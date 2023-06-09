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

#if defined(__aarch64__) || defined(__arm64__) || defined(__arm64) || defined(__ARM64) || defined(__AARCH64__)
#include <cpuinfo_aarch64.h>
namespace common
{
using CPUFeatures = cpu_features::Aarch64Features;
using CPUFeature = cpu_features::Aarch64FeaturesEnum;
using CPUInfo = cpu_features::Aarch64Info;
extern const CPUInfo cpu_info;
static inline const CPUFeatures & cpu_feature_flags = cpu_info.features;
} // namespace common
#endif

#if defined(__x86_64__) || defined(__x86_64) || defined(__amd64) || defined(__amd64__)
#include <cpuinfo_x86.h>
namespace common
{
using CPUFeatures = cpu_features::X86Features;
using CPUFeature = cpu_features::X86FeaturesEnum;
using CPUInfo = cpu_features::X86Info;
extern const CPUInfo cpu_info;
static inline const CPUFeatures & cpu_feature_flags = cpu_info.features;
} // namespace common
#endif
