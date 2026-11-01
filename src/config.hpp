#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace graph_tool {

struct Config {
    std::string package;
    std::string repository = "https://registry.npmjs.org";
    bool test_mode = false;
    std::string version;
    std::string output = "graph.svg";
    bool ascii = false;
    std::string filter;
    bool help = false;
    bool direct = false;
};

Config parse_arguments(const std::vector<std::string>& arguments);
void validate_config(const Config& config);
void print_config(const Config& config, std::ostream& output);
void print_help(std::ostream& output);

}
