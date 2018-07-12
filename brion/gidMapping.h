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

#ifndef BRION_GID_MAPPING_H
#define BRION_GID_MAPPING_H

#include <memory>

namespace brion
{
class GIDMapping
{
private:
    class Impl;

public:
    class PopulationMapping
    {
    public:
        friend class GIDMapping;
        size_t operator[](const size_t nodeID);

    private:
        class Impl;
        PopulationMapping(const GIDMapping::Impl& impl,
                          const std::string& population);
        PopulationMapping(PopulationMapping&&);
        ~PopulationMapping();
        std::unique_ptr<Impl> _impl;
    };

    GIDMapping(const std::string& filename);
    ~GIDMapping();

    GIDMapping(GIDMapping&& other);
    GIDMapping& operator=(GIDMapping&& other);

    /**
     */
    auto operator[](const std::string& population)
    {
        return std::move(PopulationMapping(*_impl, population));
    }

    /**
     */
    std::pair<const std::string&, size_t> operator[](size_t GID) const;

private:
    GIDMapping(const GIDMapping& other) = delete;
    GIDMapping& operator=(const GIDMapping& other) = delete;

    std::unique_ptr<Impl> _impl;
};
}
#endif
