/* Copyright (c) 2013-2018, EPFL/Blue Brain Project
 *                          Juan Hernando <juan.hernando@epfl.ch>
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
 * This file is part of Brion <https://github.com/BlueBrain/Brion>
 */

#include <BBP/TestDatasets.h>
#include <brion/brion.h>

#define BOOST_TEST_MODULE SimulationConfig
#include <boost/filesystem/path.hpp>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(invalid_open)
{
    BOOST_CHECK_THROW(brion::SimulationConfig("bla"), std::runtime_error);

    boost::filesystem::path path(BBP_TESTDATA);
    path /= "local/README";
    BOOST_CHECK_THROW(brion::SimulationConfig(path.string()),
                      std::runtime_error);
}

BOOST_AUTO_TEST_CASE(verify_data)
{
    boost::filesystem::path path(BBP_TESTDATA);
    path /= "sonata/simulation.json";

    brion::SimulationConfig config(path.string());

    config.getNetworkConfig();
    config.getNodeSetSource();
}