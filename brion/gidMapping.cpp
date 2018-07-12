/* Copyright (c) 2014-2018, EPFL/Blue Brain Project
 *                          Juan Hernando Vieites <jhernando@fi.upm.es>
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
 */

#include "gidMapping.h"

#include "detail/hdf5Mutex.h"
#include "detail/utilsHDF5.h"

#include <highfive/H5File.hpp>

#include <lunchbox/log.h>

#include <unordered_map>

namespace brion
{
using Mapping = std::unordered_map<size_t, size_t>;

class GIDMapping::Impl
{
public:
    Impl(const std::string& filename)
    {
        auto file = [filename]() {
            try
            {
                HighFive::SilenceHDF5 silence;
                return HighFive::File(filename, HighFive::File::ReadOnly);
            }
            catch (const HighFive::FileException& exc)
            {
                LBTHROW(std::runtime_error("Could not open GID mapping file " +
                                           filename + ": " + exc.what()));
            }
        };
    }

    std::pair<const std::string&, size_t> operator[](const size_t gid) const
    {
        return std::make_pair(_populationNames[0], gid);
    }

    std::unordered_map<std::string, Mapping> _populationMappings;
    std::vector<std::string> _populationNames;
    std::unordered_map<size_t, std::pair<size_t, size_t>> _reverseMapping;
};

class GIDMapping::PopulationMapping::Impl
{
public:
    const Mapping& mapping;

    size_t operator[](const size_t nodeID);
};

GIDMapping::PopulationMapping::PopulationMapping(const GIDMapping::Impl& impl,
                                                 const std::string& population)
{
    const auto iter = impl._populationMappings.find(population);
    if (iter == impl._populationMappings.end())
        throw std::runtime_error("Invalid population name");
    _impl.reset(new Impl{iter->second});
}

size_t GIDMapping::PopulationMapping::operator[](const size_t nodeID)
{
    const auto i = _impl->mapping.find(nodeID);
    if (i != _impl->mapping.end())
        throw std::runtime_error("Invalid node ID");
    return i->second;
}

GIDMapping::PopulationMapping::PopulationMapping(PopulationMapping&&) = default;
GIDMapping::PopulationMapping::~PopulationMapping() = default;

GIDMapping::GIDMapping(const std::string& filename)
    : _impl(new Impl(filename))
{
}

GIDMapping::~GIDMapping()
{
}

GIDMapping::GIDMapping(GIDMapping&&) = default;
GIDMapping& GIDMapping::operator=(GIDMapping&&) = default;

std::pair<const std::string&, size_t> GIDMapping::operator[](
    const size_t gid) const
{
    return _impl->operator[](gid);
}
};
