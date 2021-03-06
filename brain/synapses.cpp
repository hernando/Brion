/* Copyright (c) 2013-2018, EPFL/Blue Brain Project
 *                          Daniel.Nachbaur@epfl.ch
 *
 * This file is part of Brion <https://github.com/BlueBrain/Brion>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "synapses.h"

#include "circuit.h"
#include "synapse.h"
#include "synapsesIterator.h"
#include "synapsesStream.h"

#include "detail/circuit.h"
#include "detail/synapsesStream.h"

#include <brion/synapse.h>
#include <brion/synapseSummary.h>

#include <lunchbox/log.h>

#include <mutex>

namespace brain
{
namespace
{
template <typename T>
void _allocate(T& data, const size_t size)
{
    if (data)
        return;

    void* ptr;
    if (posix_memalign(&ptr, 32, size * sizeof(typename T::element_type)))
    {
        LBWARN << "Memory alignment failed. Trying normal allocation"
               << std::endl;
        ptr = calloc(size, sizeof(typename T::element_type));
        if (!ptr)
            LBTHROW(std::bad_alloc());
    }
    data.reset((typename T::element_type*)ptr);
    // cppcheck-suppress memleak
}

#ifdef BRION_USE_KEYV
bool _hasSurfacePositions(const Circuit::Impl& circuit)
{
    const auto& synapse = circuit.getSynapsePositions(true);
    assert(synapse.getNumAttributes() == brion::SYNAPSE_OLD_POSITION_ALL ||
           synapse.getNumAttributes() == brion::SYNAPSE_POSITION_ALL);
    return synapse.getNumAttributes() == brion::SYNAPSE_POSITION_ALL;
}
#endif
}

struct Synapses::Impl : public Synapses::BaseImpl
{
    Impl(const Circuit& circuit, const GIDSet& gids, const GIDSet& filterGIDs,
         const bool afferent, const SynapsePrefetch prefetch)
        : _circuit(circuit._impl)
        , _gids(prefetch != SynapsePrefetch::all ? gids : GIDSet())
        , _filterGIDs(prefetch != SynapsePrefetch::all ? filterGIDs : GIDSet())
        , _afferent(afferent)
        , _size(0)
    {
        _loadConnectivity(&gids, &filterGIDs);

        if (int(prefetch) & int(SynapsePrefetch::attributes))
            std::call_once(_attributeFlag, &Impl::_loadAttributes, this, &gids,
                           &filterGIDs);
        if (int(prefetch) & int(SynapsePrefetch::positions))
            std::call_once(_positionFlag, &Impl::_loadPositions, this, &gids,
                           &filterGIDs);
    }

    Impl(const Circuit& circuit, const GIDSet& gids, const std::string& source,
         const SynapsePrefetch prefetch)
        : _circuit(circuit._impl)
        , _gids(prefetch != SynapsePrefetch::all ? gids : GIDSet())
        , _afferent(true)
        , _externalSource(source)
        , _size(0)
    {
        // We don't have a summary file for projected afferent synapses.
        // But at least we have to figure out the size of the container.
        const auto& synapses =
            _circuit->getAfferentProjectionAttributes(source);
        _size = synapses.getNumSynapses(gids);

        if (int(prefetch) & int(SynapsePrefetch::attributes))
        {
            GIDSet empty;
            std::call_once(_attributeFlag, &Impl::_loadAttributes, this, &gids,
                           &empty);
        }
    }

#define FILTER(gid)                                                         \
    if (!filterGIDs->empty() && filterGIDs->find(gid) == filterGIDs->end()) \
        continue;

    void _loadConnectivity(const GIDSet* gids, const GIDSet* filterGIDs) const
    {
        const brion::SynapseSummary& synapseSummary =
            _circuit->getSynapseSummary();

        uint32_ts pres, posts;
        for (const auto gid : *gids)
        {
            const auto& summary = synapseSummary.read(gid);

            for (size_t i = 0; i < summary.shape()[0]; ++i)
            {
                const uint32_t peerGid = summary[i][0];
                FILTER(peerGid);

                for (size_t j = 0; j < summary[i][_afferent ? 2 : 1]; ++j)
                {
                    pres.push_back(peerGid);
                    posts.push_back(gid);
                }
            }
        }

        _size = pres.size();
        _allocate(_preGID, _size);
        _allocate(_postGID, _size);
        memcpy(_preGID.get(), pres.data(), _size * sizeof(uint32_t));
        memcpy(_postGID.get(), posts.data(), _size * sizeof(uint32_t));

        if (!_afferent)
            _preGID.swap(_postGID);
    }

    void _loadAttributes(const GIDSet* gids, const GIDSet* filterGIDs) const
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if (_efficacy)
            return;

        const brion::Synapse& synapseAttributes =
            _externalSource.empty()
                ? _circuit->getSynapseAttributes(_afferent)
                : _circuit->getAfferentProjectionAttributes(_externalSource);
        const brion::Synapse* synapseExtra =
            _externalSource.empty() ? _circuit->getSynapseExtra() : nullptr;

        // For external afferent projections we haven't had the chance to
        // get the connectivity.
        const bool haveGIDs = _externalSource.empty();
        if (!haveGIDs)
        {
            _allocate(_preGID, _size);
            _allocate(_postGID, _size);
        }

        const bool haveExtra = _afferent && synapseExtra;
        _allocateAttributes(_size, haveExtra || _afferent);

        size_t i = 0;
        for (const auto gid : *gids)
        {
            auto&& attr =
                synapseAttributes.read(gid, brion::SYNAPSE_ALL_ATTRIBUTES);
            auto&& extra =
                haveExtra ? synapseExtra->read(gid, 1) : brion::SynapseMatrix();
            for (size_t j = 0; j < attr.shape()[0]; ++j)
            {
                const uint32_t connected = attr[j][0];
                FILTER(connected);

                if (!haveGIDs)
                {
                    // This code path is exclusive of external projections,
                    // which are afferent only
                    assert(_afferent);
                    _preGID.get()[i] = connected;
                    _postGID.get()[i] = gid;
                }
                assert(_preGID.get()[i] == _afferent ? connected : gid);
                assert(_postGID.get()[i] == _afferent ? gid : connected);
                assert(i < _size);

                if (haveExtra)
                    _index.get()[i] = extra[j][0];
                else if (_afferent)
                    // Fallback assigment of synapses index for afferent views
                    _index.get()[i] = j;

                _delay.get()[i] = attr[j][1];
                _postSectionID.get()[i] = attr[j][2];
                _postSegmentID.get()[i] = attr[j][3];
                _postDistance.get()[i] = attr[j][4];
                _preSectionID.get()[i] = attr[j][5];
                _preSegmentID.get()[i] = attr[j][6];
                _preDistance.get()[i] = attr[j][7];
                _conductance.get()[i] = attr[j][8];
                _utilization.get()[i] = attr[j][9];
                _depression.get()[i] = attr[j][10];
                _facilitation.get()[i] = attr[j][11];
                _decay.get()[i] = attr[j][12];
                _efficacy.get()[i] = attr[j][17];
                ++i;
            }
        }
    }

    void _loadPositions(const GIDSet* gids, const GIDSet* filterGIDs) const
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_externalSource.empty())
        {
            LBTHROW(
                std::runtime_error("Synapse positions are not available "
                                   "for external projection synapses"));
        }

        if (_preCenterPositionX)
            return;

        _allocatePositions(_size);

        Strings keys;
        CachedSynapses loaded;

#if BRION_USE_KEYV
        auto cache = _circuit->getSynapseCache();
        if (cache)
        {
            keys = cache->createKeys(*gids, _afferent);
            loaded =
                cache->loadPositions(keys, _hasSurfacePositions(*_circuit));
        }
#else
        const bool cache = false;
#endif

        // delay the opening of the synapse file as much as possible, even
        // though the code looks ugly... As the circuit impl keeps the file
        // opened, we can safely just get a loose pointer here.
        const brion::Synapse* positions = nullptr;

        size_t i = 0;
        auto key = keys.begin();
        bool haveSurfacePositions = false;
        for (const auto gid : *gids)
        {
            auto it = cache ? loaded.find(*key) : loaded.end();
            const bool cached = it != loaded.end();

            const auto readFromFile = [&] {
                if (!positions)
                {
                    try
                    {
                        positions = &_circuit->getSynapsePositions(_afferent);
                    }
                    catch (...)
                    {
                        // Leave arrays unmodified for exception safety
                        _clearPositions();
                        throw;
                    }
                }

                if (positions->getNumAttributes() ==
                    brion::SYNAPSE_POSITION_ALL)
                    return positions->read(gid, brion::SYNAPSE_POSITION);
                else
                    return positions->read(gid, brion::SYNAPSE_OLD_POSITION);
            };

            const brion::SynapseMatrix pos =
                cached ? it->second : readFromFile();

#ifdef BRION_USE_KEYV
            if (cache)
            {
                if (!cached)
                    cache->savePositions(gid, *key, pos);
                ++key;
            }
#endif

            for (size_t j = 0; j < pos.size(); ++j)
            {
                const uint32_t preGid = pos[j][0];
                FILTER(preGid);

                if (pos.shape()[1] == brion::SYNAPSE_POSITION_ALL)
                {
                    haveSurfacePositions = true;
                    _preSurfacePositionX.get()[i] = pos[j][1];
                    _preSurfacePositionY.get()[i] = pos[j][2];
                    _preSurfacePositionZ.get()[i] = pos[j][3];
                    _postSurfacePositionX.get()[i] = pos[j][4];
                    _postSurfacePositionY.get()[i] = pos[j][5];
                    _postSurfacePositionZ.get()[i] = pos[j][6];
                    _preCenterPositionX.get()[i] = pos[j][7];
                    _preCenterPositionY.get()[i] = pos[j][8];
                    _preCenterPositionZ.get()[i] = pos[j][9];
                    _postCenterPositionX.get()[i] = pos[j][10];
                    _postCenterPositionY.get()[i] = pos[j][11];
                    _postCenterPositionZ.get()[i] = pos[j][12];
                }
                else
                {
                    _preCenterPositionX.get()[i] = pos[j][1];
                    _preCenterPositionY.get()[i] = pos[j][2];
                    _preCenterPositionZ.get()[i] = pos[j][3];
                    _postCenterPositionX.get()[i] = pos[j][4];
                    _postCenterPositionY.get()[i] = pos[j][5];
                    _postCenterPositionZ.get()[i] = pos[j][6];
                }
                ++i;
            }
        }

        if (!haveSurfacePositions)
        {
            _preSurfacePositionX.reset();
            _preSurfacePositionY.reset();
            _preSurfacePositionZ.reset();
            _postSurfacePositionX.reset();
            _postSurfacePositionY.reset();
            _postSurfacePositionZ.reset();
        }
    }

    void _allocateAttributes(const size_t size, const bool allocateIndex) const
    {
        if (allocateIndex)
            _allocate(_index, size);

        _allocate(_preSectionID, size);
        _allocate(_preSegmentID, size);
        _allocate(_preDistance, size);

        _allocate(_postSectionID, size);
        _allocate(_postSegmentID, size);
        _allocate(_postDistance, size);

        _allocate(_delay, size);
        _allocate(_conductance, size);
        _allocate(_utilization, size);
        _allocate(_depression, size);
        _allocate(_facilitation, size);
        _allocate(_decay, size);
        _allocate(_efficacy, size);
    }

    void _allocatePositions(const size_t size) const
    {
        _allocate(_preSurfacePositionX, size);
        _allocate(_preSurfacePositionY, size);
        _allocate(_preSurfacePositionZ, size);
        _allocate(_preCenterPositionX, size);
        _allocate(_preCenterPositionY, size);
        _allocate(_preCenterPositionZ, size);

        _allocate(_postSurfacePositionX, size);
        _allocate(_postSurfacePositionY, size);
        _allocate(_postSurfacePositionZ, size);
        _allocate(_postCenterPositionX, size);
        _allocate(_postCenterPositionY, size);
        _allocate(_postCenterPositionZ, size);
    }

    void _clearPositions() const
    {
        _preSurfacePositionX.reset();
        _preSurfacePositionY.reset();
        _preSurfacePositionZ.reset();
        _preCenterPositionX.reset();
        _preCenterPositionY.reset();
        _preCenterPositionZ.reset();
        _postSurfacePositionX.reset();
        _postSurfacePositionY.reset();
        _postSurfacePositionZ.reset();
        _postCenterPositionX.reset();
        _postCenterPositionY.reset();
        _postCenterPositionZ.reset();
    }

    void _ensureGIDs() const
    {
        if (_externalSource.empty())
            return;

        // Until C++17 call_once is using decay_copy
        std::call_once(_attributeFlag, &Impl::_loadAttributes, this, &_gids,
                       &_filterGIDs);
    }

    void _ensureAttributes() const
    {
        // Until C++17 call_once is using decay_copy
        std::call_once(_attributeFlag, &Impl::_loadAttributes, this, &_gids,
                       &_filterGIDs);
    }

    void _ensurePositions() const
    {
        // Until C++17 call_once is using decay_copy
        std::call_once(_positionFlag, &Impl::_loadPositions, this, &_gids,
                       &_filterGIDs);
    }

    size_t _getSize() const { return _size; }
    const std::shared_ptr<const Circuit::Impl> _circuit;
    const GIDSet _gids;
    const GIDSet _filterGIDs;
    const bool _afferent;
    std::string _externalSource;

    template <typename T>
    struct FreeDeleter
    {
        void operator()(T* ptr) { free(ptr); }
    };

    typedef std::unique_ptr<uint32_t, FreeDeleter<uint32_t>> UIntPtr;
    typedef std::unique_ptr<int, FreeDeleter<int>> IntPtr;
    typedef std::unique_ptr<size_t, FreeDeleter<size_t>> size_tPtr;
    typedef std::unique_ptr<float, FreeDeleter<float>> floatPtr;

    mutable size_t _size;

    mutable size_tPtr _index;

    mutable UIntPtr _preGID;
    mutable UIntPtr _preSectionID;
    mutable UIntPtr _preSegmentID;
    mutable floatPtr _preDistance;
    mutable floatPtr _preSurfacePositionX;
    mutable floatPtr _preSurfacePositionY;
    mutable floatPtr _preSurfacePositionZ;
    mutable floatPtr _preCenterPositionX;
    mutable floatPtr _preCenterPositionY;
    mutable floatPtr _preCenterPositionZ;

    mutable UIntPtr _postGID;
    mutable UIntPtr _postSectionID;
    mutable UIntPtr _postSegmentID;
    mutable floatPtr _postDistance;
    mutable floatPtr _postSurfacePositionX;
    mutable floatPtr _postSurfacePositionY;
    mutable floatPtr _postSurfacePositionZ;
    mutable floatPtr _postCenterPositionX;
    mutable floatPtr _postCenterPositionY;
    mutable floatPtr _postCenterPositionZ;

    mutable floatPtr _delay;
    mutable floatPtr _conductance;
    mutable floatPtr _utilization;
    mutable floatPtr _depression;
    mutable floatPtr _facilitation;
    mutable floatPtr _decay;
    mutable IntPtr _efficacy;

    mutable std::once_flag _attributeFlag;
    mutable std::once_flag _positionFlag;
    mutable std::once_flag _indexFlag;
    // Besides the call_once flags, we still need to ensure exclusive access
    // to state.
    mutable std::mutex _mutex;
};

Synapses::Synapses(const Circuit& circuit, const GIDSet& gids,
                   const GIDSet& filterGIDs, const bool afferent,
                   const SynapsePrefetch prefetch)
    : _impl(new Impl(circuit, gids, filterGIDs, afferent, prefetch))
{
}

Synapses::Synapses(const Circuit& circuit, const GIDSet& gids,
                   const std::string& source, const SynapsePrefetch prefetch)
    : _impl(new Impl(circuit, gids, source, prefetch))
{
}

Synapses::~Synapses()
{
}

Synapses::Synapses(const SynapsesStream& stream)
    : _impl(stream._impl->_externalSource.empty()
                ? new Impl(stream._impl->_circuit, stream._impl->_gids,
                           stream._impl->_filterGIDs, stream._impl->_afferent,
                           stream._impl->_prefetch)
                : new Impl(stream._impl->_circuit, stream._impl->_gids,
                           stream._impl->_externalSource,
                           stream._impl->_prefetch))
{
}

Synapses::Synapses(const Synapses& rhs) noexcept : _impl(rhs._impl)
{
}

Synapses::Synapses(Synapses&& rhs) noexcept : _impl(std::move(rhs._impl))
{
}

Synapses& Synapses::operator=(const Synapses& rhs) noexcept
{
    if (this != &rhs)
        _impl = rhs._impl;
    return *this;
}

Synapses& Synapses::operator=(Synapses&& rhs) noexcept
{
    if (this != &rhs)
        _impl = std::move(rhs._impl);
    return *this;
}

size_t Synapses::size() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    return impl._getSize();
}

bool Synapses::empty() const
{
    return size() == 0;
}

Synapses::const_iterator Synapses::begin() const
{
    return const_iterator(*this, 0);
}

Synapses::const_iterator Synapses::end() const
{
    return const_iterator(*this, size());
}

Synapse Synapses::operator[](const size_t index_) const
{
    return Synapse(*this, index_);
}

const size_t* Synapses::indices() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    if (!impl._index)
        LBTHROW(std::runtime_error("Synapse index not available"));
    return impl._index.get();
}

const uint32_t* Synapses::preGIDs() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureGIDs();
    return impl._preGID.get();
}

const uint32_t* Synapses::preSectionIDs() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._preSectionID.get();
}

const uint32_t* Synapses::preSegmentIDs() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._preSegmentID.get();
}

const float* Synapses::preDistances() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._preDistance.get();
}

const float* Synapses::preSurfaceXPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._preSurfacePositionX.get();
}

const float* Synapses::preSurfaceYPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._preSurfacePositionY.get();
}

const float* Synapses::preSurfaceZPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._preSurfacePositionZ.get();
}

const float* Synapses::preCenterXPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._preCenterPositionX.get();
}

const float* Synapses::preCenterYPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._preCenterPositionY.get();
}

const float* Synapses::preCenterZPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._preCenterPositionZ.get();
}

const uint32_t* Synapses::postGIDs() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureGIDs();
    return impl._postGID.get();
}

const uint32_t* Synapses::postSectionIDs() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._postSectionID.get();
}

const uint32_t* Synapses::postSegmentIDs() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._postSegmentID.get();
}

const float* Synapses::postDistances() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._postDistance.get();
}

const float* Synapses::postSurfaceXPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._postSurfacePositionX.get();
}

const float* Synapses::postSurfaceYPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._postSurfacePositionY.get();
}

const float* Synapses::postSurfaceZPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._postSurfacePositionZ.get();
}

const float* Synapses::postCenterXPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._postCenterPositionX.get();
}

const float* Synapses::postCenterYPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._postCenterPositionY.get();
}

const float* Synapses::postCenterZPositions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensurePositions();
    return impl._postCenterPositionZ.get();
}

const float* Synapses::delays() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._delay.get();
}

const float* Synapses::conductances() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._conductance.get();
}

const float* Synapses::utilizations() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._utilization.get();
}

const float* Synapses::depressions() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._depression.get();
}

const float* Synapses::facilitations() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._facilitation.get();
}

const float* Synapses::decays() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._decay.get();
}

const int* Synapses::efficacies() const
{
    const Impl& impl = static_cast<const Impl&>(*_impl);
    impl._ensureAttributes();
    return impl._efficacy.get();
}
}
