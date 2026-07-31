#include <tsunami/adapters/gmsh/GmshMeshImporter.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include <tsunami/core/Error.hpp>

namespace tsunami::adapters::gmsh
{
    namespace
    {
        using Tag = std::int64_t;

        constexpr auto operation_name = "import_gmsh_msh41_ascii_mesh";
        constexpr auto rule_id = "SWE-GEO-MSH-WP1";
        constexpr auto geometry_tolerance = 1.0e-12;

        constexpr auto domain_name = std::string_view{"region.domain"};
        constexpr auto offshore_name = std::string_view{"boundary.offshore"};
        constexpr auto inland_name = std::string_view{"boundary.inland"};
        constexpr auto left_side_name = std::string_view{"boundary.left_side"};
        constexpr auto right_side_name = std::string_view{"boundary.right_side"};

        const auto mandatory_names = std::array<std::string_view, 5U>{
            domain_name,
            offshore_name,
            inland_name,
            left_side_name,
            right_side_name};

        const auto boundary_names = std::array<std::string_view, 4U>{
            offshore_name,
            inland_name,
            left_side_name,
            right_side_name};

        struct EntityKey
        {
            int dimension{};
            Tag tag{};

            friend auto operator<(const EntityKey &left, const EntityKey &right) noexcept -> bool
            {
                if (left.dimension != right.dimension) {
                    return left.dimension < right.dimension;
                }
                return left.tag < right.tag;
            }
        };

        struct EntityRecord
        {
            std::vector<Tag> physical_tags;
        };

        struct NodeRecord
        {
            tsunami::fvm::Point3 position;
        };

        struct TriangleRecord
        {
            Tag element_tag{};
            std::array<Tag, 3U> node_tags{};
        };

        struct BoundaryLineRecord
        {
            Tag element_tag{};
            std::array<Tag, 2U> node_tags{};
            std::string physical_name;
        };

        struct ParsedMesh
        {
            std::string version;
            std::map<std::string, Tag> physical_name_tags;
            std::map<Tag, std::string> physical_tag_names;
            std::map<EntityKey, EntityRecord> entities;
            std::map<Tag, NodeRecord> nodes;
            std::vector<TriangleRecord> triangles;
            std::vector<BoundaryLineRecord> boundary_lines;
        };

        struct EdgeKey
        {
            tsunami::core::Index first{};
            tsunami::core::Index second{};

            friend auto operator<(const EdgeKey &left, const EdgeKey &right) noexcept -> bool
            {
                if (left.first != right.first) {
                    return left.first < right.first;
                }
                return left.second < right.second;
            }
        };

        struct EdgeUse
        {
            tsunami::fvm::CellId cell;
            tsunami::fvm::VertexId first;
            tsunami::fvm::VertexId second;
        };

        [[nodiscard]] auto import_error(
            const std::filesystem::path &path,
            std::string code,
            std::string message,
            std::string entity_type = {},
            std::string entity_id = {}) -> tsunami::core::Error
        {
            auto error = tsunami::core::Error{
                std::move(code),
                std::move(message),
                tsunami::core::DiagnosticCategory::input_data,
                tsunami::core::Severity::error};
            error.add_context("operation", operation_name)
                .add_context("rule_id", rule_id)
                .add_context("source_path", path.string())
                .add_context("state_changed", "false");
            if (!entity_type.empty()) {
                error.add_context("entity_type", std::move(entity_type));
            }
            if (!entity_id.empty()) {
                error.add_context("entity_id", std::move(entity_id));
            }
            return error;
        }

        [[nodiscard]] auto trim_cr(std::string value) -> std::string
        {
            if (!value.empty() && value.back() == '\r') {
                value.pop_back();
            }
            return value;
        }

        template <class T>
        [[nodiscard]] auto read_value(std::istringstream &stream, T &out) -> bool
        {
            stream >> out;
            return !stream.fail();
        }

        [[nodiscard]] auto parse_tokens(std::string_view line) -> std::vector<std::string>
        {
            auto stream = std::istringstream{std::string{line}};
            auto tokens = std::vector<std::string>{};
            auto token = std::string{};
            while (stream >> token) {
                tokens.push_back(std::move(token));
            }
            return tokens;
        }

        [[nodiscard]] auto to_tag(const std::string &token) -> std::optional<Tag>
        {
            auto stream = std::istringstream{token};
            auto out = Tag{};
            stream >> out;
            if (stream.fail()) {
                return std::nullopt;
            }
            return out;
        }

        [[nodiscard]] auto is_mandatory_name(std::string_view name) noexcept -> bool
        {
            return std::find(mandatory_names.begin(), mandatory_names.end(), name) != mandatory_names.end();
        }

        [[nodiscard]] auto is_boundary_name(std::string_view name) noexcept -> bool
        {
            return std::find(boundary_names.begin(), boundary_names.end(), name) != boundary_names.end();
        }

        [[nodiscard]] auto has_physical_tag(const EntityRecord *entity, Tag tag) -> bool
        {
            return entity != nullptr &&
                std::find(entity->physical_tags.begin(), entity->physical_tags.end(), tag) != entity->physical_tags.end();
        }

        [[nodiscard]] auto entity_for(const ParsedMesh &mesh, int dimension, Tag tag) -> const EntityRecord *
        {
            const auto found = mesh.entities.find(EntityKey{dimension, tag});
            if (found == mesh.entities.end()) {
                return nullptr;
            }
            return &found->second;
        }

        [[nodiscard]] auto boundary_physical_name(const ParsedMesh &mesh, const EntityRecord *entity)
            -> std::optional<std::string>
        {
            if (entity == nullptr) {
                return std::nullopt;
            }
            auto out = std::optional<std::string>{};
            for (const auto tag : entity->physical_tags) {
                const auto found = mesh.physical_tag_names.find(tag);
                if (found == mesh.physical_tag_names.end() || !is_boundary_name(found->second)) {
                    continue;
                }
                if (out.has_value()) {
                    return std::string{};
                }
                out = found->second;
            }
            return out;
        }

        [[nodiscard]] auto parse_physical_name_line(
            const std::filesystem::path &path,
            std::string_view line,
            ParsedMesh &mesh) -> tsunami::core::Result<void>
        {
            const auto first_quote = line.find('"');
            const auto last_quote = line.rfind('"');
            if (first_quote == std::string_view::npos || last_quote == first_quote) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "physical name line is malformed", "section", "PhysicalNames"));
            }
            auto prefix = std::istringstream{std::string{line.substr(0U, first_quote)}};
            auto dimension = int{};
            auto tag = Tag{};
            if (!read_value(prefix, dimension) || !read_value(prefix, tag)) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "physical name line is malformed", "section", "PhysicalNames"));
            }
            const auto name = std::string{line.substr(first_quote + 1U, last_quote - first_quote - 1U)};
            if (is_mandatory_name(name) && mesh.physical_name_tags.contains(name)) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.physical_name_duplicate", "mandatory physical name is duplicated", "physical_name", name));
            }
            mesh.physical_name_tags.emplace(name, tag);
            mesh.physical_tag_names.emplace(tag, name);
            return tsunami::core::success();
        }

        [[nodiscard]] auto require_line(
            const std::filesystem::path &path,
            const std::vector<std::string> &lines,
            std::size_t &index,
            std::string_view section) -> tsunami::core::Result<std::string>
        {
            if (index >= lines.size()) {
                return tsunami::core::failure<std::string>(import_error(path, "mesh.gmsh.section_parse_failed", "section ended unexpectedly", "section", std::string{section}));
            }
            return tsunami::core::success(lines[index++]);
        }

        [[nodiscard]] auto read_scalar_tokens(
            const std::filesystem::path &path,
            const std::vector<std::string> &lines,
            std::size_t &index,
            std::size_t count,
            std::string_view section) -> tsunami::core::Result<std::vector<std::string>>
        {
            auto out = std::vector<std::string>{};
            while (out.size() < count) {
                auto line = require_line(path, lines, index, section);
                if (!line) {
                    return tsunami::core::failure<std::vector<std::string>>(line.error());
                }
                auto tokens = parse_tokens(line.value());
                out.insert(out.end(), std::make_move_iterator(tokens.begin()), std::make_move_iterator(tokens.end()));
            }
            if (out.size() != count) {
                return tsunami::core::failure<std::vector<std::string>>(import_error(path, "mesh.gmsh.section_parse_failed", "token count does not match section header", "section", std::string{section}));
            }
            return tsunami::core::success(std::move(out));
        }

        [[nodiscard]] auto parse_mesh_format(
            const std::filesystem::path &path,
            const std::vector<std::string> &lines,
            std::size_t &index,
            ParsedMesh &mesh) -> tsunami::core::Result<void>
        {
            auto line = require_line(path, lines, index, "MeshFormat");
            if (!line) {
                return tsunami::core::failure(line.error());
            }
            auto stream = std::istringstream{line.value()};
            auto file_type = int{};
            auto data_size = int{};
            if (!(stream >> mesh.version >> file_type >> data_size)) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "mesh format line is malformed", "section", "MeshFormat"));
            }
            if (mesh.version != "4.1") {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.version_unsupported", "only Gmsh MSH version 4.1 is supported", "version", mesh.version));
            }
            if (file_type != 0) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.binary_unsupported", "binary Gmsh MSH files are not supported", "file_type", std::to_string(file_type)));
            }
            line = require_line(path, lines, index, "MeshFormat");
            if (!line || line.value() != "$EndMeshFormat") {
                return tsunami::core::failure(line ? import_error(path, "mesh.gmsh.section_parse_failed", "mesh format section terminator is missing", "section", "MeshFormat") : line.error());
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto parse_physical_names(
            const std::filesystem::path &path,
            const std::vector<std::string> &lines,
            std::size_t &index,
            ParsedMesh &mesh) -> tsunami::core::Result<void>
        {
            auto count_line = require_line(path, lines, index, "PhysicalNames");
            if (!count_line) {
                return tsunami::core::failure(count_line.error());
            }
            auto count_stream = std::istringstream{count_line.value()};
            auto count = std::size_t{};
            if (!(count_stream >> count)) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "physical name count is malformed", "section", "PhysicalNames"));
            }
            for (std::size_t i = 0U; i < count; ++i) {
                auto line = require_line(path, lines, index, "PhysicalNames");
                if (!line) {
                    return tsunami::core::failure(line.error());
                }
                if (auto parsed = parse_physical_name_line(path, line.value(), mesh); !parsed) {
                    return parsed;
                }
            }
            auto end = require_line(path, lines, index, "PhysicalNames");
            if (!end || end.value() != "$EndPhysicalNames") {
                return tsunami::core::failure(end ? import_error(path, "mesh.gmsh.section_parse_failed", "physical names section terminator is missing", "section", "PhysicalNames") : end.error());
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto parse_entity_line(
            const std::filesystem::path &path,
            const std::string &line,
            int dimension,
            ParsedMesh &mesh) -> tsunami::core::Result<void>
        {
            auto tokens = parse_tokens(line);
            const auto minimum = dimension == 0 ? 5U : 8U;
            if (tokens.size() < minimum) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "entity line is malformed", "section", "Entities"));
            }
            auto tag = to_tag(tokens[0U]);
            if (!tag) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "entity tag is malformed", "section", "Entities"));
            }
            const auto physical_count_index = dimension == 0 ? 4U : 7U;
            auto physical_count = to_tag(tokens[physical_count_index]);
            if (!physical_count || *physical_count < 0) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "entity physical-tag count is malformed", "section", "Entities"));
            }
            if (tokens.size() < physical_count_index + 1U + static_cast<std::size_t>(*physical_count)) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "entity physical-tag list is truncated", "section", "Entities"));
            }
            auto entity = EntityRecord{};
            for (std::size_t i = 0U; i < static_cast<std::size_t>(*physical_count); ++i) {
                auto physical = to_tag(tokens[physical_count_index + 1U + i]);
                if (!physical) {
                    return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "entity physical tag is malformed", "section", "Entities"));
                }
                entity.physical_tags.push_back(*physical);
            }
            mesh.entities.emplace(EntityKey{dimension, *tag}, std::move(entity));
            return tsunami::core::success();
        }

        [[nodiscard]] auto parse_entities(
            const std::filesystem::path &path,
            const std::vector<std::string> &lines,
            std::size_t &index,
            ParsedMesh &mesh) -> tsunami::core::Result<void>
        {
            auto header = require_line(path, lines, index, "Entities");
            if (!header) {
                return tsunami::core::failure(header.error());
            }
            auto stream = std::istringstream{header.value()};
            auto counts = std::array<std::size_t, 4U>{};
            if (!(stream >> counts[0U] >> counts[1U] >> counts[2U] >> counts[3U])) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "entities header is malformed", "section", "Entities"));
            }
            for (int dimension = 0; dimension < 4; ++dimension) {
                for (std::size_t i = 0U; i < counts[static_cast<std::size_t>(dimension)]; ++i) {
                    auto line = require_line(path, lines, index, "Entities");
                    if (!line) {
                        return tsunami::core::failure(line.error());
                    }
                    if (auto parsed = parse_entity_line(path, line.value(), dimension, mesh); !parsed) {
                        return parsed;
                    }
                }
            }
            auto end = require_line(path, lines, index, "Entities");
            if (!end || end.value() != "$EndEntities") {
                return tsunami::core::failure(end ? import_error(path, "mesh.gmsh.section_parse_failed", "entities section terminator is missing", "section", "Entities") : end.error());
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto parse_nodes(
            const std::filesystem::path &path,
            const std::vector<std::string> &lines,
            std::size_t &index,
            ParsedMesh &mesh) -> tsunami::core::Result<void>
        {
            auto header = require_line(path, lines, index, "Nodes");
            if (!header) {
                return tsunami::core::failure(header.error());
            }
            auto stream = std::istringstream{header.value()};
            auto block_count = std::size_t{};
            auto node_count = std::size_t{};
            auto min_tag = Tag{};
            auto max_tag = Tag{};
            if (!(stream >> block_count >> node_count >> min_tag >> max_tag)) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "nodes header is malformed", "section", "Nodes"));
            }
            for (std::size_t block = 0U; block < block_count; ++block) {
                auto block_header = require_line(path, lines, index, "Nodes");
                if (!block_header) {
                    return tsunami::core::failure(block_header.error());
                }
                auto block_stream = std::istringstream{block_header.value()};
                auto entity_dimension = int{};
                auto entity_tag = Tag{};
                auto parametric = int{};
                auto nodes_in_block = std::size_t{};
                if (!(block_stream >> entity_dimension >> entity_tag >> parametric >> nodes_in_block)) {
                    return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "node block header is malformed", "section", "Nodes"));
                }
                auto node_tag_tokens = read_scalar_tokens(path, lines, index, nodes_in_block, "Nodes");
                if (!node_tag_tokens) {
                    return tsunami::core::failure(node_tag_tokens.error());
                }
                const auto values_per_node = static_cast<std::size_t>(3 + (parametric == 0 ? 0 : entity_dimension));
                auto coordinate_tokens = read_scalar_tokens(path, lines, index, nodes_in_block * values_per_node, "Nodes");
                if (!coordinate_tokens) {
                    return tsunami::core::failure(coordinate_tokens.error());
                }
                for (std::size_t i = 0U; i < nodes_in_block; ++i) {
                    auto tag = to_tag(node_tag_tokens.value()[i]);
                    if (!tag) {
                        return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "node tag is malformed", "section", "Nodes"));
                    }
                    auto coordinate = std::array<double, 3U>{};
                    for (std::size_t component = 0U; component < 3U; ++component) {
                        auto coordinate_stream = std::istringstream{coordinate_tokens.value()[i * values_per_node + component]};
                        coordinate_stream >> coordinate[component];
                        if (coordinate_stream.fail() || !std::isfinite(coordinate[component])) {
                            return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "node coordinate is malformed", "node", std::to_string(*tag)));
                        }
                    }
                    if (!mesh.nodes.emplace(*tag, NodeRecord{tsunami::fvm::Point3{coordinate[0U], coordinate[1U], coordinate[2U]}}).second) {
                        return tsunami::core::failure(import_error(path, "mesh.gmsh.node_duplicate", "node tag is duplicated", "node", std::to_string(*tag)));
                    }
                }
            }
            if (mesh.nodes.size() != node_count) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "node count does not match nodes header", "section", "Nodes"));
            }
            auto end = require_line(path, lines, index, "Nodes");
            if (!end || end.value() != "$EndNodes") {
                return tsunami::core::failure(end ? import_error(path, "mesh.gmsh.section_parse_failed", "nodes section terminator is missing", "section", "Nodes") : end.error());
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto parse_elements(
            const std::filesystem::path &path,
            const std::vector<std::string> &lines,
            std::size_t &index,
            ParsedMesh &mesh) -> tsunami::core::Result<void>
        {
            auto header = require_line(path, lines, index, "Elements");
            if (!header) {
                return tsunami::core::failure(header.error());
            }
            auto stream = std::istringstream{header.value()};
            auto block_count = std::size_t{};
            auto element_count = std::size_t{};
            auto min_tag = Tag{};
            auto max_tag = Tag{};
            if (!(stream >> block_count >> element_count >> min_tag >> max_tag)) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "elements header is malformed", "section", "Elements"));
            }
            auto parsed_elements = std::size_t{};
            const auto domain_tag = mesh.physical_name_tags.at(std::string{domain_name});
            for (std::size_t block = 0U; block < block_count; ++block) {
                auto block_header = require_line(path, lines, index, "Elements");
                if (!block_header) {
                    return tsunami::core::failure(block_header.error());
                }
                auto block_stream = std::istringstream{block_header.value()};
                auto entity_dimension = int{};
                auto entity_tag = Tag{};
                auto element_type = int{};
                auto elements_in_block = std::size_t{};
                if (!(block_stream >> entity_dimension >> entity_tag >> element_type >> elements_in_block)) {
                    return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "element block header is malformed", "section", "Elements"));
                }
                const auto *entity = entity_for(mesh, entity_dimension, entity_tag);
                const auto is_domain_entity = entity_dimension == 2 && has_physical_tag(entity, domain_tag);
                const auto boundary_name = boundary_physical_name(mesh, entity);
                if (boundary_name && boundary_name->empty()) {
                    return tsunami::core::failure(import_error(path, "mesh.gmsh.boundary_entity_ambiguous", "boundary entity has multiple mandatory physical names", "entity", std::to_string(entity_tag)));
                }
                const auto is_imported_boundary_entity = entity_dimension == 1 && boundary_name.has_value();
                const auto is_imported_entity = is_domain_entity || is_imported_boundary_entity;
                if (is_imported_entity && element_type != 1 && element_type != 2) {
                    return tsunami::core::failure(import_error(path, "mesh.gmsh.element_type_unsupported", "only first-order line and triangle elements are supported in imported entities", "element_type", std::to_string(element_type)));
                }
                if (element_type == 2 && !is_domain_entity) {
                    return tsunami::core::failure(import_error(path, "mesh.gmsh.triangle_region_missing", "triangle element is not assigned to region.domain", "entity", std::to_string(entity_tag)));
                }
                for (std::size_t element = 0U; element < elements_in_block; ++element) {
                    auto line = require_line(path, lines, index, "Elements");
                    if (!line) {
                        return tsunami::core::failure(line.error());
                    }
                    ++parsed_elements;
                    auto tokens = parse_tokens(line.value());
                    if (element_type == 1 && is_imported_boundary_entity) {
                        if (tokens.size() != 3U) {
                            return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "line element is malformed", "section", "Elements"));
                        }
                        auto element_tag = to_tag(tokens[0U]);
                        auto first = to_tag(tokens[1U]);
                        auto second = to_tag(tokens[2U]);
                        if (!element_tag || !first || !second) {
                            return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "line element contains invalid tags", "section", "Elements"));
                        }
                        mesh.boundary_lines.push_back(BoundaryLineRecord{*element_tag, {*first, *second}, *boundary_name});
                    } else if (element_type == 2) {
                        if (tokens.size() != 4U) {
                            return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "triangle element is malformed", "section", "Elements"));
                        }
                        auto element_tag = to_tag(tokens[0U]);
                        auto first = to_tag(tokens[1U]);
                        auto second = to_tag(tokens[2U]);
                        auto third = to_tag(tokens[3U]);
                        if (!element_tag || !first || !second || !third) {
                            return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "triangle element contains invalid tags", "section", "Elements"));
                        }
                        mesh.triangles.push_back(TriangleRecord{*element_tag, {*first, *second, *third}});
                    }
                }
            }
            if (parsed_elements != element_count) {
                return tsunami::core::failure(import_error(path, "mesh.gmsh.section_parse_failed", "element count does not match elements header", "section", "Elements"));
            }
            auto end = require_line(path, lines, index, "Elements");
            if (!end || end.value() != "$EndElements") {
                return tsunami::core::failure(end ? import_error(path, "mesh.gmsh.section_parse_failed", "elements section terminator is missing", "section", "Elements") : end.error());
            }
            return tsunami::core::success();
        }

        [[nodiscard]] auto read_parsed_mesh(const std::filesystem::path &path) -> tsunami::core::Result<ParsedMesh>
        {
            auto file = std::ifstream{path};
            if (!file) {
                return tsunami::core::failure<ParsedMesh>(import_error(path, "mesh.gmsh.open_failed", "failed to open Gmsh MSH file"));
            }
            auto lines = std::vector<std::string>{};
            auto line = std::string{};
            while (std::getline(file, line)) {
                lines.push_back(trim_cr(std::move(line)));
            }

            auto mesh = ParsedMesh{};
            auto saw_mesh_format = false;
            auto saw_physical_names = false;
            auto saw_entities = false;
            auto saw_nodes = false;
            auto saw_elements = false;
            for (std::size_t index = 0U; index < lines.size();) {
                const auto section = lines[index++];
                if (section == "$MeshFormat") {
                    saw_mesh_format = true;
                    if (auto parsed = parse_mesh_format(path, lines, index, mesh); !parsed) {
                        return tsunami::core::failure<ParsedMesh>(parsed.error());
                    }
                } else if (section == "$PhysicalNames") {
                    saw_physical_names = true;
                    if (auto parsed = parse_physical_names(path, lines, index, mesh); !parsed) {
                        return tsunami::core::failure<ParsedMesh>(parsed.error());
                    }
                } else if (section == "$Entities") {
                    saw_entities = true;
                    if (auto parsed = parse_entities(path, lines, index, mesh); !parsed) {
                        return tsunami::core::failure<ParsedMesh>(parsed.error());
                    }
                } else if (section == "$Nodes") {
                    saw_nodes = true;
                    if (auto parsed = parse_nodes(path, lines, index, mesh); !parsed) {
                        return tsunami::core::failure<ParsedMesh>(parsed.error());
                    }
                } else if (section == "$Elements") {
                    saw_elements = true;
                    for (const auto name : mandatory_names) {
                        if (!mesh.physical_name_tags.contains(std::string{name})) {
                            return tsunami::core::failure<ParsedMesh>(import_error(path, "mesh.gmsh.physical_name_missing", "mandatory physical name is missing", "physical_name", std::string{name}));
                        }
                    }
                    if (auto parsed = parse_elements(path, lines, index, mesh); !parsed) {
                        return tsunami::core::failure<ParsedMesh>(parsed.error());
                    }
                } else if (section.empty()) {
                    continue;
                } else if (section.starts_with("$End")) {
                    return tsunami::core::failure<ParsedMesh>(import_error(path, "mesh.gmsh.section_parse_failed", "unexpected section terminator", "section", section));
                }
            }
            if (!saw_mesh_format || !saw_physical_names || !saw_entities || !saw_nodes || !saw_elements) {
                return tsunami::core::failure<ParsedMesh>(import_error(path, "mesh.gmsh.section_missing", "required Gmsh MSH section is missing"));
            }
            for (const auto name : mandatory_names) {
                if (!mesh.physical_name_tags.contains(std::string{name})) {
                    return tsunami::core::failure<ParsedMesh>(import_error(path, "mesh.gmsh.physical_name_missing", "mandatory physical name is missing", "physical_name", std::string{name}));
                }
            }
            return tsunami::core::success(std::move(mesh));
        }

        [[nodiscard]] auto signed_area2(
            const tsunami::fvm::Point3 &first,
            const tsunami::fvm::Point3 &second,
            const tsunami::fvm::Point3 &third) noexcept -> double
        {
            return ((second.x - first.x) * (third.y - first.y)) -
                ((third.x - first.x) * (second.y - first.y));
        }

        [[nodiscard]] auto edge_key(tsunami::fvm::VertexId first, tsunami::fvm::VertexId second) noexcept -> EdgeKey
        {
            return first.value < second.value ? EdgeKey{first.value, second.value} : EdgeKey{second.value, first.value};
        }

        [[nodiscard]] auto boundary_patch_id(std::string_view name) -> std::optional<tsunami::fvm::BoundaryPatchId>
        {
            for (std::size_t i = 0U; i < boundary_names.size(); ++i) {
                if (boundary_names[i] == name) {
                    return tsunami::fvm::BoundaryPatchId{i};
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] auto make_mesh_input(
            const std::filesystem::path &path,
            ParsedMesh mesh,
            GmshMeshImportMetadata &metadata) -> tsunami::core::Result<tsunami::fvm::MeshTopologyInput>
        {
            auto node_to_vertex = std::map<Tag, tsunami::fvm::VertexId>{};
            auto vertices = std::vector<tsunami::fvm::VertexRecord>{};
            vertices.reserve(mesh.nodes.size());
            for (const auto &[tag, node] : mesh.nodes) {
                const auto id = tsunami::fvm::VertexId{vertices.size()};
                node_to_vertex.emplace(tag, id);
                vertices.push_back(tsunami::fvm::VertexRecord{id, node.position});
            }

            std::sort(mesh.triangles.begin(), mesh.triangles.end(), [](const auto &left, const auto &right) {
                return left.element_tag < right.element_tag;
            });

            auto triangle_vertices = std::vector<std::array<tsunami::fvm::VertexId, 3U>>{};
            triangle_vertices.reserve(mesh.triangles.size());
            for (const auto &triangle : mesh.triangles) {
                auto ids = std::array<tsunami::fvm::VertexId, 3U>{};
                for (std::size_t i = 0U; i < ids.size(); ++i) {
                    const auto found = node_to_vertex.find(triangle.node_tags[i]);
                    if (found == node_to_vertex.end()) {
                        return tsunami::core::failure<tsunami::fvm::MeshTopologyInput>(import_error(path, "mesh.gmsh.node_reference_missing", "triangle references a missing node", "node", std::to_string(triangle.node_tags[i])));
                    }
                    ids[i] = found->second;
                }
                const auto &first = vertices[ids[0U].value].position;
                const auto &second = vertices[ids[1U].value].position;
                const auto &third = vertices[ids[2U].value].position;
                const auto area2 = signed_area2(first, second, third);
                if (std::abs(area2) <= geometry_tolerance) {
                    return tsunami::core::failure<tsunami::fvm::MeshTopologyInput>(import_error(path, "mesh.gmsh.degenerate_triangle", "triangle area must be non-zero", "element", std::to_string(triangle.element_tag)));
                }
                if (area2 < 0.0) {
                    std::swap(ids[1U], ids[2U]);
                    ++metadata.clockwise_triangle_count;
                }
                triangle_vertices.push_back(ids);
            }

            auto edge_uses = std::map<EdgeKey, std::vector<EdgeUse>>{};
            auto edge_face_ids = std::map<EdgeKey, tsunami::fvm::FaceId>{};
            auto edge_order = std::vector<EdgeKey>{};
            auto cells = std::vector<tsunami::fvm::CellRecord>{};
            cells.reserve(triangle_vertices.size());
            for (std::size_t cell_index = 0U; cell_index < triangle_vertices.size(); ++cell_index) {
                const auto cell_id = tsunami::fvm::CellId{cell_index};
                const auto &triangle = triangle_vertices[cell_index];
                auto cell_faces = std::vector<tsunami::fvm::FaceId>{};
                for (const auto edge : std::array<std::array<tsunami::fvm::VertexId, 2U>, 3U>{
                         std::array<tsunami::fvm::VertexId, 2U>{triangle[0U], triangle[1U]},
                         std::array<tsunami::fvm::VertexId, 2U>{triangle[1U], triangle[2U]},
                         std::array<tsunami::fvm::VertexId, 2U>{triangle[2U], triangle[0U]}}) {
                    const auto key = edge_key(edge[0U], edge[1U]);
                    auto [face_id, inserted] = edge_face_ids.emplace(key, tsunami::fvm::FaceId{edge_face_ids.size()});
                    if (inserted) {
                        edge_order.push_back(key);
                    }
                    edge_uses[key].push_back(EdgeUse{cell_id, edge[0U], edge[1U]});
                    if (edge_uses[key].size() > 2U) {
                        return tsunami::core::failure<tsunami::fvm::MeshTopologyInput>(import_error(path, "mesh.gmsh.non_manifold_face", "edge is referenced by more than two triangles", "edge", std::to_string(key.first) + ":" + std::to_string(key.second)));
                    }
                    cell_faces.push_back(face_id->second);
                }
                cells.push_back(tsunami::fvm::CellRecord{cell_id, std::move(cell_faces)});
            }

            auto boundary_edges = std::map<EdgeKey, std::string>{};
            for (const auto &line : mesh.boundary_lines) {
                auto first = node_to_vertex.find(line.node_tags[0U]);
                auto second = node_to_vertex.find(line.node_tags[1U]);
                if (first == node_to_vertex.end() || second == node_to_vertex.end()) {
                    const auto missing = first == node_to_vertex.end() ? line.node_tags[0U] : line.node_tags[1U];
                    return tsunami::core::failure<tsunami::fvm::MeshTopologyInput>(import_error(path, "mesh.gmsh.node_reference_missing", "boundary line references a missing node", "node", std::to_string(missing)));
                }
                const auto key = edge_key(first->second, second->second);
                const auto use = edge_uses.find(key);
                if (use == edge_uses.end()) {
                    return tsunami::core::failure<tsunami::fvm::MeshTopologyInput>(import_error(path, "mesh.gmsh.boundary_line_unmatched", "boundary line does not match a reconstructed cell face", "element", std::to_string(line.element_tag)));
                }
                if (use->second.size() != 1U) {
                    return tsunami::core::failure<tsunami::fvm::MeshTopologyInput>(import_error(path, "mesh.gmsh.boundary_line_not_boundary", "boundary line matches an internal face", "element", std::to_string(line.element_tag)));
                }
                if (!boundary_edges.emplace(key, line.physical_name).second) {
                    return tsunami::core::failure<tsunami::fvm::MeshTopologyInput>(import_error(path, "mesh.gmsh.boundary_line_duplicate", "boundary line duplicates an existing boundary face assignment", "element", std::to_string(line.element_tag)));
                }
            }

            auto patch_faces = std::array<std::vector<tsunami::fvm::FaceId>, 4U>{};
            auto faces = std::vector<tsunami::fvm::FaceRecord>{};
            faces.reserve(edge_order.size());
            for (const auto &key : edge_order) {
                const auto face_id = edge_face_ids.at(key);
                const auto &uses = edge_uses.at(key);
                auto owner = uses.front().cell;
                auto neighbour = std::optional<tsunami::fvm::CellId>{};
                if (uses.size() == 2U) {
                    owner = uses[0U].cell.value < uses[1U].cell.value ? uses[0U].cell : uses[1U].cell;
                    neighbour = uses[0U].cell.value < uses[1U].cell.value ? uses[1U].cell : uses[0U].cell;
                }
                auto patch = std::optional<tsunami::fvm::BoundaryPatchId>{};
                if (uses.size() == 1U) {
                    const auto boundary = boundary_edges.find(key);
                    if (boundary == boundary_edges.end()) {
                        return tsunami::core::failure<tsunami::fvm::MeshTopologyInput>(import_error(path, "mesh.gmsh.boundary_face_unassigned", "reconstructed boundary face has no mandatory boundary line", "face", std::to_string(face_id.value)));
                    }
                    patch = boundary_patch_id(boundary->second);
                    if (!patch) {
                        return tsunami::core::failure<tsunami::fvm::MeshTopologyInput>(import_error(path, "mesh.gmsh.boundary_face_unassigned", "boundary face physical name is not mandatory", "face", std::to_string(face_id.value)));
                    }
                    patch_faces[patch->value].push_back(face_id);
                }
                faces.push_back(tsunami::fvm::FaceRecord{
                    face_id,
                    {uses.front().first, uses.front().second},
                    owner,
                    neighbour,
                    patch});
            }

            auto boundary_patches = std::vector<tsunami::fvm::BoundaryPatchRecord>{};
            boundary_patches.reserve(boundary_names.size());
            for (std::size_t i = 0U; i < boundary_names.size(); ++i) {
                boundary_patches.push_back(tsunami::fvm::BoundaryPatchRecord{
                    tsunami::fvm::BoundaryPatchId{i},
                    std::string{boundary_names[i]},
                    std::move(patch_faces[i])});
            }

            return tsunami::core::success(tsunami::fvm::MeshTopologyInput{
                tsunami::fvm::MeshId{"gmsh:" + path.filename().string()},
                2U,
                std::move(vertices),
                std::move(faces),
                std::move(cells),
                std::move(boundary_patches)});
        }
    } // namespace

    auto import_gmsh_msh41_ascii_mesh(const std::filesystem::path &path)
        -> tsunami::core::Result<GmshMeshImportResult>
    {
        auto parsed = read_parsed_mesh(path);
        if (!parsed) {
            return tsunami::core::failure<GmshMeshImportResult>(parsed.error());
        }
        auto metadata = GmshMeshImportMetadata{};
        metadata.source_path = path;
        metadata.msh_version = parsed.value().version;
        metadata.imported_node_count = static_cast<std::uint64_t>(parsed.value().nodes.size());
        metadata.triangle_count = static_cast<std::uint64_t>(parsed.value().triangles.size());
        metadata.boundary_line_count = static_cast<std::uint64_t>(parsed.value().boundary_lines.size());
        for (const auto name : mandatory_names) {
            const auto key = std::string{name};
            metadata.physical_name_tags.emplace(key, parsed.value().physical_name_tags.at(key));
        }

        auto input = make_mesh_input(path, std::move(parsed).value(), metadata);
        if (!input) {
            return tsunami::core::failure<GmshMeshImportResult>(input.error());
        }
        auto mesh = tsunami::fvm::make_finite_volume_mesh(std::move(input).value());
        if (!mesh) {
            return tsunami::core::failure<GmshMeshImportResult>(mesh.error());
        }
        return tsunami::core::success(GmshMeshImportResult{std::move(mesh).value(), std::move(metadata)});
    }

} // namespace tsunami::adapters::gmsh
