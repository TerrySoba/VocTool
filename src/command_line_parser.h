#ifndef COMMAND_LINE_PARSER_H
#define COMMAND_LINE_PARSER_H

#include <stdexcept>
#include <string>
#include <optional>
#include <vector>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <optional>

namespace clp
{

enum class ParameterRequired {
    no,
    yes
};

struct Parameter
{
    std::string name;
    std::string shortForm;
    std::string description;
    ParameterRequired required;
    std::optional<std::string> defaultValue;
    std::optional<std::string> value;
};

std::string makeParameterString(auto shortForm, auto name)
{
    std::stringstream ss;
    ss << "-" << shortForm << ", --" << name;
    return ss.str();
}

std::string makeSpaces(int num)
{
    std::string spaces(num, ' ');
    return spaces;
}

class CommandLineParser
{
public:
    CommandLineParser(const std::string& programDescription) :
        m_programDescription(programDescription)
    {
    }

    void addParameter(
        const std::string& name,
        const std::string& shortForm,
        const std::string& description,
        ParameterRequired required,
        std::optional<std::string> defaultValue = {})
    {
        m_parameters.push_back(Parameter{name, shortForm, description, required, defaultValue, {}});
    }

    void parse(int argc, char* argv[])
    {
        std::string programName = (argc > 0) ? argv[0] : "(unknown)";
        for (int i = 1; i < argc; ++i)
        {
            std::string param = argv[i];
            if (param.size() < 2 || param[0] != '-')
            {
                std::cout << "invalid parameter \"" << param << "\"\n";
                printUsage(programName);
                exit(1);
            }

            Parameter* parameter = nullptr;

            if (param[1] == '-') // long parameter
            {
                std::string name = param.substr(2);
                auto it = std::find_if(m_parameters.begin(), m_parameters.end(), [name](auto value){return value.name == name;});
                if (it != m_parameters.end())
                {
                    parameter = &(*it);
                }
            }
            else // short parameter
            {
                std::string name = param.substr(1);
                auto it = std::find_if(m_parameters.begin(), m_parameters.end(), [name](auto value){return value.shortForm == name;});
                if (it != m_parameters.end())
                {
                    parameter = &(*it);
                }
            }

            if (!parameter) 
            {
                std::cout << "unknown parameter \"" << param << "\"\n";
                printUsage(programName);
                exit(1);
            }
            
            if (i + 1 >= argc)
            {
                std::cout << "missing string after parameter \"" << param << "\"\n";
                printUsage(programName);
                exit(1);
            }

            ++i;
            parameter->value = argv[i];
        }

        // check if parameters are missing
        for (auto& param : m_parameters)
        {
            if (!param.value.has_value() && param.required == ParameterRequired::yes)
            {
                std::cout << "Parameter \"" << param.name << "\" is missing.\n";
                printUsage(programName);
                exit(1);
            }
            if (!param.value.has_value())
            {
                param.value = param.defaultValue;
            }
        }

    }

    bool hasValue(const std::string& name) const
    {
        auto it = std::find_if(m_parameters.begin(), m_parameters.end(), [name](auto value){return value.name == name;});
        if (it == m_parameters.end())
        {
            std::stringstream message;
            message << "No parameter with name \"" << name << "\" is was found.\n";
            throw std::logic_error(message.str());
        }

        return it->value.has_value();
    }

    template <typename T>
    std::optional<T> getValueOptional(const std::string& name) const
    {
        auto it = std::find_if(m_parameters.begin(), m_parameters.end(), [name](auto value){return value.name == name;});
        if (it == m_parameters.end())
        {
            std::stringstream message;
            message << "No parameter with name \"" << name << "\" is was found.\n";
            throw std::logic_error(message.str());
        }

        if (!it->value.has_value())
        {
            return {};
        }

        std::stringstream ss;
        ss << *it->value;
        T ret;
        ss >> ret;
        return ret;
    }

    template <typename T>
    T getValue(const std::string& name) const
    {
        auto val = getValueOptional<T>(name);

        if (!val.has_value())
        {
            std::stringstream message;
            message << "Parameter \"" << name << "\" is missing.\n";
            throw std::logic_error(message.str());
        }

        return *val;
    }

private:
    void printUsage(const std::string& programName)
    {
        std::cout << "Usage: " << programName << " ";
        
        for (auto param : m_parameters)
        {
            if (param.required == ParameterRequired::no) {
                std::cout << "[ ";
            }

            std::string uppercaseName = param.name;
            for (auto& c: uppercaseName) c = toupper(c);

            std::cout << "-" << param.shortForm << " " << uppercaseName << " ";

            if (param.required == ParameterRequired::no) {
                std::cout << "] ";
            }
        }

        std::cout << "\n\n" << m_programDescription << "\n\n";
        std::cout << "options:\n";

        unsigned int longestLine = 0;

        for (auto param : m_parameters)
        {
            std::string line = makeParameterString(param.shortForm, param.name);
            if (line.size() > longestLine)
            {
                longestLine = line.size();
            }
        }

        for (auto param : m_parameters)
        {
            std::string line = makeParameterString(param.shortForm, param.name);
            std::cout << "  " << line << makeSpaces(longestLine - line.size()) << "  " << param.description;

            if (param.required == ParameterRequired::no && param.defaultValue.has_value())
            {
                std::cout << " ( default: " << param.defaultValue.value_or("") << " )";
            }

            std::cout << "\n";
        }
        std::cout << "\n";
    }

    std::string m_programDescription;
    std::vector<Parameter> m_parameters;
};


}

#endif