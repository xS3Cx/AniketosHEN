#pragma once

#include <onion/payload_catalog.hpp>
#include <onion/payload_catalog_policy.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace onion::payload_catalog {

struct SourcesFile {
  std::vector<Source> sources;
};

SourcesFile default_sources();
bool parse_sources(std::string_view json, SourcesFile *out);
std::string serialize_sources(const SourcesFile &sources);
bool load_sources_file(const char *path, SourcesFile *out);
bool save_sources_file(const char *path, const SourcesFile &sources);
bool add_source(SourcesFile *sources, std::string_view name, std::string_view url,
                std::string *error = nullptr);
bool remove_source(SourcesFile *sources, std::string_view url);

}
