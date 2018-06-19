/* Copyright (c) 2018, EPFL/Blue Brain Project
 *                     Juan Hernando <juan.hernando@epfl.ch>
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

#ifndef BRION_SIMULATIONCONFIG
#define BRION_SIMULATIONCONFIG

#include <brion/api.h>
#include <brion/types.h>

namespace brion
{
/** Read access to a SONATA simulation config file.
 */
class SimulationConfig
{
public:
    BRION_API ~SimulationConfig();

    BRION_API SimulationConfig(const std::string& source);

    BRION_API SimulationConfig(SimulationConfig&&);
    BRION_API SimulationConfig& operator=(SimulationConfig&&);

    std::string getNetworkConfig() const;

    std::string getNodeSetSource() const;

    Strings getCompartmentReportNames() const;

    std::string getOutputRoot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
}
#endif
