// Copyright 2022 PingCAP, Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <Poco/Event.h>
#include <common/MultiVersion.h>

#include <functional>
#include <thread>


namespace Poco
{
class Logger;
namespace Util
{
class AbstractConfiguration;
}
} // namespace Poco

class RegionsHierarchies;
class TechDataHierarchy;
class RegionsNames;
class IGeoDictionariesLoader;


namespace DB
{
class Context;


/// Metrica's Dictionaries which can be used in functions.

class EmbeddedDictionaries
{
private:
    Poco::Logger * log;
    Context & context;

    MultiVersion<RegionsHierarchies> regions_hierarchies;
    MultiVersion<TechDataHierarchy> tech_data_hierarchy;
    MultiVersion<RegionsNames> regions_names;

    std::unique_ptr<IGeoDictionariesLoader> geo_dictionaries_loader;

    /// Directories' updating periodicity (in seconds).
    int reload_period;
    int cur_reload_period = 1;
    bool is_fast_start_stage = true;

    mutable std::mutex mutex;

    std::thread reloading_thread;
    Poco::Event destroy;


    void handleException(const bool throw_on_error) const;

    /** Updates directories (dictionaries) every reload_period seconds.
     * If all dictionaries are not loaded at least once, try reload them with exponential delay (1, 2, ... reload_period).
     * If all dictionaries are loaded, update them using constant reload_period delay.
     */
    void reloadPeriodically();

    /// Updates dictionaries.
    bool reloadImpl(const bool throw_on_error, const bool force_reload = false);

    template <typename Dictionary>
    using DictionaryReloader = std::function<std::unique_ptr<Dictionary>(const Poco::Util::AbstractConfiguration & config)>;

    template <typename Dictionary>
    bool reloadDictionary(
        MultiVersion<Dictionary> & dictionary,
        DictionaryReloader<Dictionary> reload_dictionary,
        const bool throw_on_error,
        const bool force_reload);

public:
    /// Every reload_period seconds directories are updated inside a separate thread.
    EmbeddedDictionaries(
        std::unique_ptr<IGeoDictionariesLoader> geo_dictionaries_loader,
        Context & context,
        const bool throw_on_error);

    /// Forcibly reloads all dictionaries.
    void reload();

    ~EmbeddedDictionaries();


    MultiVersion<RegionsHierarchies>::Version getRegionsHierarchies() const
    {
        return regions_hierarchies.get();
    }

    MultiVersion<TechDataHierarchy>::Version getTechDataHierarchy() const
    {
        return tech_data_hierarchy.get();
    }

    MultiVersion<RegionsNames>::Version getRegionsNames() const
    {
        return regions_names.get();
    }
};

} // namespace DB
