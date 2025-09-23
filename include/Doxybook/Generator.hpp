#pragma once
#include "JsonConverter.hpp"
#include "Doxygen.hpp"
#include "Renderer.hpp"
#include <string>
#include <unordered_set>

namespace Doxybook2 {
    class Generator {
    public:
        typedef std::unordered_set<Kind> Filter;

        struct SummarySection {
            FolderCategory type;
            Filter filter;
            Filter skip;
        };

        explicit Generator(const Config& config,
            const Doxygen& doxygen,
            const JsonConverter& jsonConverter,
            const std::optional<std::string>& templatesPath);

        void print(const Filter& filter, const Filter& skip);
        void json(const Filter& filter, const Filter& skip);
        void manifest();
        void printIndex(FolderCategory type, const Filter& filter, const Filter& skip);
        void summary(const std::string& inputFile,
            const std::string& outputFile,
            const std::vector<SummarySection>& sections);

    private:
        void printRecursively(const Node& parent, const Filter& filter, const Filter& skip);
        nlohmann::json manifestRecursively(const Node& node);
        void jsonRecursively(const Node& parent, const Filter& filter, const Filter& skip);
        std::string kindToTemplateName(Kind kind);
        nlohmann::json buildIndexRecursively(const Node& node, const Filter& filter, const Filter& skip);
        void summaryRecursive(std::stringstream& ss,
            int indent,
            const std::string& folderName,
            const Node& node,
            const Filter& filter,
            const Filter& skip);
        bool shouldInclude(const Node& node);
        std::string getWikiFileName(const Node& node);
        void buildWikiNameMapping(const Node& parent, const Filter& filter, const Filter& skip);
        
    public:
        // Allow other classes to access the mapping
        const std::unordered_map<std::string, std::string>& getWikiNameMapping() const {
            return refidToWikiName;
        }
        
        // Get the wiki-safe filename for a refid
        std::string getWikiFileNameForRefid(const std::string& refid) const {
            auto it = refidToWikiName.find(refid);
            if (it != refidToWikiName.end()) {
                return it->second;
            }
            return refid; // Fall back to refid if not found
        }
        
    private:

        const Config& config;
        const Doxygen& doxygen;
        const JsonConverter& jsonConverter;
        Renderer renderer;
        std::unordered_map<std::string, std::string> refidToWikiName;
    };
} // namespace Doxybook2
