#include "ExceptionUtils.hpp"
#include <Doxybook/Doxygen.hpp>
#include <Doxybook/Exception.hpp>
#include <Doxybook/Generator.hpp>
#include <Doxybook/Path.hpp>
#include <Doxybook/Renderer.hpp>
#include <Doxybook/Utils.hpp>
#include <filesystem>
#include <fstream>
#include <inja/inja.hpp>
#include <spdlog/spdlog.h>

std::string Doxybook2::Generator::kindToTemplateName(const Kind kind) {
    using namespace Doxybook2;
    switch (kind) {
        case Kind::STRUCT:
            return config.templateKindStruct;
        case Kind::INTERFACE:
            return config.templateKindInterface;
        case Kind::UNION:
            return config.templateKindUnion;
        case Kind::CLASS:
            return config.templateKindClass;
        case Kind::NAMESPACE:
            return config.templateKindNamespace;
        case Kind::MODULE:
            return config.templateKindGroup;
        case Kind::DIR:
            return config.templateKindDir;
        case Kind::FILE:
            return config.templateKindFile;
        case Kind::PAGE:
            return config.templateKindPage;
        case Kind::EXAMPLE:
            return config.templateKindExample;
        case Kind::JAVAENUM:
            return config.templateKindJavaEnum;
        default: {
            throw EXCEPTION("Unrecognised kind {} please contant the author!", int(kind));
        }
    }
}

Doxybook2::Generator::Generator(const Config& config,
    const Doxygen& doxygen,
    const JsonConverter& jsonConverter,
    const std::optional<std::string>& templatesPath)
    : config(config), doxygen(doxygen), jsonConverter(jsonConverter),
      renderer(config, doxygen, jsonConverter, templatesPath) {
    // Give JsonConverter access to this Generator instance
    const_cast<JsonConverter&>(jsonConverter).setGenerator(this);
    
    // Build the wiki name mapping if wiki naming conventions are enabled
    if (config.useWikiNamingConventions) {
        spdlog::info("Wiki naming conventions enabled. Building mapping...");
        
        // Build mapping for all kinds of nodes
        Filter allKinds = {
            Kind::CLASS, Kind::STRUCT, Kind::UNION, Kind::INTERFACE, Kind::NAMESPACE,
            Kind::FILE, Kind::DIR, Kind::PAGE, Kind::MODULE, Kind::EXAMPLE,
            Kind::JAVAENUM
        };
        Filter noSkip = {};
        
        // First, recursively visit all nodes to build the mapping
        buildWikiNameMapping(doxygen.getIndex(), allKinds, noSkip);
        
        // Print the mapping for debugging
        spdlog::info("Wiki name mapping built with {} entries.", refidToWikiName.size());
        spdlog::info("Global mapping has {} entries.", g_refidToFilename.size());
        
        // Ensure the global mapping is complete by copying all entries from refidToWikiName to g_refidToFilename
        for (const auto& [refid, wikiName] : refidToWikiName) {
            g_refidToFilename[refid] = wikiName;
        }
        
        spdlog::info("Global mapping now has {} entries.", g_refidToFilename.size());
        
        // Print some entries for debugging
        int count = 0;
        for (const auto& [refid, wikiName] : g_refidToFilename) {
            spdlog::debug("  {} -> {}", refid, wikiName);
            if (++count >= 10) {
                spdlog::debug("  ... and {} more entries", g_refidToFilename.size() - 10);
                break;
            }
        }
    }
}

void Doxybook2::Generator::summary(const std::string& inputFile,
    const std::string& outputFile,
    const std::vector<SummarySection>& sections) {

    std::ifstream input(inputFile);
    if (!input) {
        throw EXCEPTION("File {} failed to open for reading", inputFile);
    }

    std::ofstream output(outputFile);
    if (!output) {
        throw EXCEPTION("File {} failed to open for writing", outputFile);
    }

    static const auto compare = [](const char* a, const char* b) {
        while (*a && *b) {
            if (*a++ != *b++)
                return false;
        }
        return true;
    };

    std::string tmpl((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto offset = tmpl.size();
    size_t indent = 0;
    for (size_t i = 0; i < tmpl.size(); i++) {
        const auto& c = tmpl[i];
        if (compare(&tmpl[i], "{{doxygen}}")) {
            offset = i;
            break;
        }
        if (c == ' ')
            indent++;
        else
            indent = 0;
    }

    std::stringstream ss;

    for (const auto& section : sections) {
        const auto name = typeToIndexTitle(config, section.type);
        const auto path = typeToIndexName(config, section.type) + "." + config.fileExt;
        ss << std::string(indent, ' ') << "* [" << name << "](" << path << ")\n";
        summaryRecursive(ss, indent + 2, name, doxygen.getIndex(), section.filter, section.skip);
    }

    output << tmpl.substr(0, offset);

    output << ss.str().substr(indent);

    if (offset + ::strlen("{{doxygen}}") < tmpl.size()) {
        output << tmpl.substr(offset + ::strlen("{{doxygen}}"));
    }
}

void Doxybook2::Generator::summaryRecursive(std::stringstream& ss,
    const int indent,
    const std::string& folderName,
    const Node& node,
    const Filter& filter,
    const Filter& skip) {

    for (const auto& child : node.getChildren()) {
        if (child->getKind() == Kind::PAGE && child->getRefid() == config.mainPageName) {
            continue;
        }
        if (filter.find(child->getKind()) != filter.end()) {
            if (skip.find(child->getKind()) == skip.end() && shouldInclude(*child)) {
                std::string filename;
                if (config.useWikiNamingConventions) {
                    filename = getWikiFileName(*child);
                } else {
                    filename = child->getRefid();
                }

                ss << std::string(indent, ' ') << "* [" << child->getName() << "](" << folderName << "/" << filename
                   << ".md)\n";
            }
            summaryRecursive(ss, indent, folderName, *child, filter, skip);
        }
    }
}

void Doxybook2::Generator::printRecursively(const Node& parent, const Filter& filter, const Filter& skip) {
    // The mapping should already be built in the constructor

    for (const auto& child : parent.getChildren()) {
        if (filter.find(child->getKind()) != filter.end()) {
            if (skip.find(child->getKind()) == skip.end() && shouldInclude(*child)) {
                nlohmann::json data = jsonConverter.getAsJson(*child);

                std::string filename;
                if (config.useWikiNamingConventions) {
                    filename = getWikiFileName(*child);
                } else {
                    filename = child->getRefid();
                }

                std::string path;
                if (child->getKind() == Kind::PAGE && child->getRefid() == config.mainPageName) {
                    path = filename + "." + config.fileExt;
                } else if (config.useFolders) {
                    path = Path::join(typeToFolderName(config, child->getType()), filename + "." + config.fileExt);
                } else {
                    path = filename + "." + config.fileExt;
                }

                renderer.render(kindToTemplateName(child->getKind()), path, data);
            }
            printRecursively(*child, filter, skip);
        }
    }
}

void Doxybook2::Generator::jsonRecursively(const Node& parent, const Filter& filter, const Filter& skip) {
    // The mapping should already be built in the constructor

    for (const auto& child : parent.getChildren()) {
        if (filter.find(child->getKind()) != filter.end()) {
            if (skip.find(child->getKind()) == skip.end() && shouldInclude(*child)) {
                nlohmann::json data = jsonConverter.getAsJson(*child);

                std::string filename;
                if (config.useWikiNamingConventions) {
                    filename = getWikiFileName(*child);
                } else {
                    filename = child->getRefid();
                }

                const auto path = Path::join(config.outputDir, filename + ".json");

                spdlog::info("Rendering {}", path);
                std::ofstream file(path);
                if (!file)
                    throw EXCEPTION("File {} failed to open for writing", path);

                file << data.dump(2);
            }
            jsonRecursively(*child, filter, skip);
        }
    }
}

void Doxybook2::Generator::print(const Filter& filter, const Filter& skip) {
    printRecursively(doxygen.getIndex(), filter, skip);
}

void Doxybook2::Generator::json(const Filter& filter, const Filter& skip) {
    jsonRecursively(doxygen.getIndex(), filter, skip);
}

void Doxybook2::Generator::manifest() {
    auto data = manifestRecursively(doxygen.getIndex());
    const auto path = Path::join(config.outputDir, "manifest.json");

    spdlog::info("Rendering {}", path);
    std::ofstream file(path);
    if (!file)
        throw EXCEPTION("File {} failed to open for writing", path);

    file << data.dump(2);
}

nlohmann::json Doxybook2::Generator::manifestRecursively(const Node& node) {
    auto ret = nlohmann::json::array();
    for (const auto& child : node.getChildren()) {
        if (!shouldInclude(*child)) {
            continue;
        }

        nlohmann::json data;
        data["kind"] = toStr(child->getKind());
        data["name"] = child->getName();
        if (child->getKind() == Kind::MODULE)
            data["title"] = child->getTitle();
        data["url"] = child->getUrl();

        ret.push_back(std::move(data));

        auto test = manifestRecursively(*child);
        if (!test.empty()) {
            ret.back()["children"] = std::move(test);
        }
    }
    return ret;
}

void Doxybook2::Generator::printIndex(const FolderCategory type, const Filter& filter, const Filter& skip) {
    const auto path = typeToIndexName(config, type) + "." + config.fileExt;

    nlohmann::json data;
    data["children"] = buildIndexRecursively(doxygen.getIndex(), filter, skip);
    data["title"] = typeToIndexTitle(config, type);
    data["name"] = typeToIndexTitle(config, type);
    renderer.render(typeToIndexTemplate(config, type), path, data);
}

nlohmann::json Doxybook2::Generator::buildIndexRecursively(const Node& node, const Filter& filter, const Filter& skip) {
    auto json = nlohmann::json::array();
    std::vector<const Node*> sorted;
    sorted.reserve(node.getChildren().size());

    for (const auto& child : node.getChildren()) {
        if (filter.find(child->getKind()) != filter.end() && shouldInclude(*child)) {
            sorted.push_back(child.get());
        }
    }

    std::sort(
        sorted.begin(), sorted.end(), [](const Node* a, const Node* b) -> bool { return a->getName() < b->getName(); });

    for (const auto& child : sorted) {
        auto data = jsonConverter.convert(*child);

        auto test = buildIndexRecursively(*child, filter, skip);
        if (!test.empty()) {
            data["children"] = std::move(test);
        }

        json.push_back(std::move(data));
    }

    return json;
}

std::string Doxybook2::Generator::getWikiFileName(const Node& node) {
    const auto& refid = node.getRefid();

    // If already in map, return it
    auto it = refidToWikiName.find(refid);
    if (it != refidToWikiName.end()) {
        // Make sure the global mapping is also updated
        g_refidToFilename[refid] = it->second;
        spdlog::debug("Found existing wiki name for refid '{}': '{}'", refid, it->second);
        return it->second;
    }

    // For file nodes, use the qualified name which contains the actual filename
    std::string nodeName;
    if (node.isFileOrDir()) {
        nodeName = node.getQualifiedName();
        spdlog::debug("Using qualified name for file/dir node '{}': '{}'", refid, nodeName);
    } else {
        // For other nodes, use the name or title if available
        nodeName = !node.getTitle().empty() ? node.getTitle() : node.getName();
        spdlog::debug("Using title/name for node '{}': '{}'", refid, nodeName);
    }

    // Create wiki-safe name from the node's name
    std::string wikiName = Utils::wikiSafeFileName(nodeName);
    spdlog::debug("Wiki-safe name for '{}' is '{}'", nodeName, wikiName);

    // If the name is empty (unlikely but possible), use the refid
    if (wikiName.empty()) {
        wikiName = Utils::wikiSafeFileName(refid);
        spdlog::debug("Empty wiki name, using refid instead: '{}'", wikiName);
    }

    // Ensure uniqueness within folder
    // We'll add a numeric suffix if needed
    std::string baseName = wikiName;
    int suffix = 1;

    // Check if this name is already used by another node in the same folder
    bool isDuplicate = false;
    for (const auto& [existingRefid, existingName] : refidToWikiName) {
        if (existingName == wikiName) {
            // Check if they would be in the same folder
            bool sameFolder = false;
            auto existingNode = doxygen.find(existingRefid);
            if (existingNode) {
                sameFolder = existingNode->getType() == node.getType();
            }

            if (sameFolder) {
                isDuplicate = true;
                spdlog::debug("Duplicate name '{}' found in same folder", wikiName);
                break;
            }
        }
    }

    // If duplicate, add suffix until we find a unique name
    while (isDuplicate) {
        wikiName = baseName + "-" + std::to_string(suffix++);
        spdlog::debug("Trying new name with suffix: '{}'", wikiName);

        // Check if this new name is unique
        isDuplicate = false;
        for (const auto& [existingRefid, existingName] : refidToWikiName) {
            if (existingName == wikiName) {
                // Check if they would be in the same folder
                bool sameFolder = false;
                auto existingNode = doxygen.find(existingRefid);
                if (existingNode) {
                    sameFolder = existingNode->getType() == node.getType();
                }

                if (sameFolder) {
                    isDuplicate = true;
                    break;
                }
            }
        }
    }

    // Store in map and return
    refidToWikiName[refid] = wikiName;
    // Also update the global mapping
    g_refidToFilename[refid] = wikiName;
    spdlog::debug("Added mapping: '{}' -> '{}'", refid, wikiName);
    return wikiName;
}

void Doxybook2::Generator::buildWikiNameMapping(const Node& parent, const Filter& filter, const Filter& skip) {
    // Process all children
    for (const auto& child : parent.getChildren()) {
        // Process this node regardless of filter to ensure all nodes are mapped
        std::string wikiName = getWikiFileName(*child);
        // Make sure the global mapping is updated
        g_refidToFilename[child->getRefid()] = wikiName;
        spdlog::debug("Built mapping for '{}' -> '{}'", child->getRefid(), wikiName);
        
        // Recursively process all children
        buildWikiNameMapping(*child, filter, skip);
    }
}

bool Doxybook2::Generator::shouldInclude(const Node& node) {
    switch (node.getKind()) {
        case Kind::FILE: {
            if (config.filesFilter.empty()) {
                return true;
            }

            const auto ext = std::filesystem::path(node.getName()).extension().string();
            const auto found = std::find(config.filesFilter.begin(), config.filesFilter.end(), ext);

            return found != config.filesFilter.end();
        }
        default: {
            return true;
        }
    }
}
