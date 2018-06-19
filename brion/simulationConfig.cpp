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

#include "simulationConfig.h"
#include "detail/utils.h"

namespace brion
{
struct SimulationConfig::Impl
{
    std::string networkConfig;
    std::string nodeSets;
    std::string outputRoot;
    Strings reportNames;

    Impl(const std::string& uri)
        : _resolver(uri)
    {
        const auto json = parseSonataJson(uri);

        try
        {
            networkConfig = _resolver.toAbsolute(json.at("network"));
        }
        catch (nlohmann::detail::exception& e)
        {
            // Check if this configuration is a circuit configuration as well.
            // Otherwise report an error about the missing network field.
            if (json.find("networks") == json.end())
                throw std::runtime_error(
                    "Error parsing simulation config: network not specified");
            networkConfig = uri;
        }

        if (json.find("node_sets_file") != json.end())
            nodeSets = _resolver.toAbsolute(json["node_sets_file"]);

        try
        {
            outputRoot =
                _resolver.toAbsolute(json.at("output").at("output_dir"));
            const auto reports = json.find("reports");
            // Can't use range-based for
            if (reports != json.end())
            {
                for (auto report = reports->begin(); report != reports->end();
                     ++report)
                {
                    // Consider only reports with module "membrame_report"
                    try
                    {
                        if (report->at("module") == "membrame_report")
                            reportNames.push_back(report.key());
                    }
                    catch (nlohmann::detail::exception& e)
                    {
                        // ignore
                    }
                }
            }
        }
        catch (nlohmann::detail::exception& e)
        {
            throw std::runtime_error(
                (std::string("Error parsing simulation config: ") + e.what())
                    .c_str());
        }
    }

private:
    PathResolver _resolver;
};

SimulationConfig::SimulationConfig(const std::string& source)
    : _impl(new SimulationConfig::Impl(source))
{
}

SimulationConfig::~SimulationConfig() = default;

SimulationConfig::SimulationConfig(SimulationConfig&&) = default;
SimulationConfig& SimulationConfig::operator=(SimulationConfig&&) = default;

std::string SimulationConfig::getNetworkConfig() const
{
    return _impl->networkConfig;
}

std::string SimulationConfig::getNodeSetSource() const
{
    return _impl->nodeSets;
}

Strings SimulationConfig::getCompartmentReportNames() const
{
    return _impl->reportNames;
}

std::string SimulationConfig::getOutputRoot() const
{
    return _impl->outputRoot;
}
}
