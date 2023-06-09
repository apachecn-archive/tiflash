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

#include <Dictionaries/CatBoostModel.h>
#include <Interpreters/ExternalLoader.h>
#include <common/logger_useful.h>

#include <memory>


namespace DB
{
class Context;

/// Manages user-defined models.
class ExternalModels : public ExternalLoader
{
public:
    using ModelPtr = std::shared_ptr<IModel>;

    /// Models will be loaded immediately and then will be updated in separate thread, each 'reload_period' seconds.
    ExternalModels(
        std::unique_ptr<IExternalLoaderConfigRepository> config_repository,
        Context & context,
        bool throw_on_error);

    /// Forcibly reloads specified model.
    void reloadModel(const std::string & name) { reload(name); }

    ModelPtr getModel(const std::string & name) const
    {
        return std::static_pointer_cast<IModel>(getLoadable(name));
    }

protected:
    std::unique_ptr<IExternalLoadable> create(const std::string & name, const Configuration & config, const std::string & config_prefix) override;

    using ExternalLoader::getObjectsMap;

    friend class StorageSystemModels;

private:
    Context & context;
};

} // namespace DB
