#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <unordered_map>

using namespace std;
namespace fs = std::filesystem;

template<typename Iterator>
string join(Iterator begin, Iterator end, const string& separator) {
    ostringstream oss;
    if(begin != end) {
        oss << *begin++;
        while(begin != end)
            oss << separator << *begin++;
    }
    return oss.str();
}

enum class ArrayElementType { NUMBER, BOOL, STRING };

struct ArrayElement {
    ArrayElementType type;
    double numberValue;
    string stringValue;
};

struct FunctionInfo {
    vector<string> body;
    vector<pair<string, string>> parameters;
    unordered_map<string, string> localStrings;
    unordered_map<string, double> localVariables;
};

string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

string removeLineComments(const string &line) {
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '\"') {
            if (i == 0 || line[i-1] != '\\')
                inQuote = !inQuote;
        }
        if (!inQuote && i + 1 < line.size() && line[i] == '/' && line[i+1] == '/') {
            return line.substr(0, i);
        }
    }
    return line;
}

vector<string> splitArgs(const string &argsStr) {
    vector<string> args;
    string current;
    bool inQuotes = false;
    for (char c : argsStr) {
        if (c == '\"') {
            inQuotes = !inQuotes;
            current.push_back(c);
        } else if (c == ',' && !inQuotes) {
            args.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) args.push_back(trim(current));
    return args;
}

void reportError(int lineNumber, const string &origLine, const string &description) {
    cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\nBuilding failed on " << lineNumber 
         << " line:\n  \"" << origLine << "\" - unable to " << description 
         << "\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << endl;
}

class ExpressionParser {
public:
    ExpressionParser(const string &expr, 
                    const unordered_map<string, double> &vars,
                    const unordered_map<string, vector<ArrayElement>> &arrs,
                    const unordered_map<string, string> &strVars = {})
        : s(expr), pos(0), variables(vars), arrays(arrs), stringVariables(strVars) {}

    double parse() {
        double result = parseExpression();
        skipSpaces();
        if (pos != s.size()) throw runtime_error("Unexpected characters at end of expression");
        return result;
    }

private:
    string s;
    size_t pos;
    const unordered_map<string, double>& variables;
    const unordered_map<string, vector<ArrayElement>>& arrays;
    const unordered_map<string, string>& stringVariables;

    void skipSpaces() { 
        while (pos < s.size() && isspace(s[pos])) pos++; 
    }

    double parseIdentifier() {
        size_t start = pos;
        while (pos < s.size() && (isalnum(s[pos]) || s[pos] == '_' || s[pos] == '[')) pos++;
        string ident = s.substr(start, pos - start);
        
        if (ident == "true") return 1.0;
        if (ident == "false") return 0.0;
        
        size_t bracketPos = ident.find('[');
        if (bracketPos != string::npos) {
            string arrayName = ident.substr(0, bracketPos);
            string indexExpr = ident.substr(bracketPos + 1, ident.size() - bracketPos - 2);
            try {
                ExpressionParser indexParser(indexExpr, variables, arrays, stringVariables);
                double index = indexParser.parse();
                int idx = static_cast<int>(index);
                if (!arrays.count(arrayName))
                    throw runtime_error("Array '" + arrayName + "' not found");
                if (idx < 0 || idx >= arrays.at(arrayName).size())
                    throw runtime_error("Index out of bounds");
                ArrayElement elem = arrays.at(arrayName)[idx];
                if (elem.type == ArrayElementType::STRING)
                    throw runtime_error("Cannot use string in numeric expression");
                return elem.numberValue;
            } catch (exception &e) {
                throw runtime_error("Array access error: " + string(e.what()));
            }
        }
        
        auto varIt = variables.find(ident);
        if (varIt != variables.end()) return varIt->second;
        
        auto strIt = stringVariables.find(ident);
        if (strIt != stringVariables.end())
            throw runtime_error("Cannot use string variable in numeric context");
        
        throw runtime_error("Undefined variable: " + ident);
    }

    double parseExpression() {
        double result = parseTerm();
        while (true) {
            skipSpaces();
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
                char op = s[pos++];
                double term = parseTerm();
                result = (op == '+') ? result + term : result - term;
            } else break;
        }
        return result;
    }

    double parseTerm() {
        double result = parseFactor();
        while (true) {
            skipSpaces();
            if (pos < s.size() && (s[pos] == '*' || s[pos] == '/')) {
                char op = s[pos++];
                double factor = parseFactor();
                result = (op == '*') ? result * factor : result / factor;
            } else break;
        }
        return result;
    }

    double parseFactor() {
        skipSpaces();
        if (pos < s.size() && s[pos] == '!') {
            pos++;
            double value = parseFactor();
            return (value == 0.0) ? 1.0 : 0.0;
        }
        if (pos < s.size() && s[pos] == '(') {
            pos++;
            double result = parseExpression();
            skipSpaces();
            if (pos >= s.size() || s[pos] != ')') 
                throw runtime_error("Missing closing parenthesis");
            pos++;
            return result;
        }
        if (pos < s.size() && (isalpha(s[pos]) || s[pos] == '_'))
            return parseIdentifier();
        return parseNumber();
    }

    double parseNumber() {
        skipSpaces();
        size_t start = pos;
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
        bool dotFound = false;
        while (pos < s.size() && (isdigit(s[pos]) || s[pos] == '.')) {
            if (s[pos] == '.') {
                if (dotFound) break;
                dotFound = true;
            }
            pos++;
        }
        if (start == pos) throw runtime_error("Expected number");
        return stod(s.substr(start, pos - start));
    }
};

string formatNumber(double num) {
    ostringstream oss;
    if (fabs(num - round(num)) < 1e-9) oss << static_cast<long long>(round(num));
    else oss << num;
    return oss.str();
}

string llvmGlobalString(const string &str, const string &name, int actualLen) {
    string escaped;
    for (char c : str) {
        if (c == '\\') escaped += "\\5C";
        else if (c == '\"') escaped += "\\22";
        else if (c == '\n') escaped += "\\0A";
        else if (c == '\r') escaped += "\\0D";
        else if (c < 32 || c > 126) {
            char buf[5];
            snprintf(buf, sizeof(buf), "\\%02X", static_cast<unsigned char>(c));
            escaped += buf;
        } else escaped.push_back(c);
    }
    return "@" + name + " = private constant [" + to_string(actualLen) + " x i8] c\"" + escaped + "\\00\"";
}

size_t skipBlock(const vector<string>& lines, size_t startIndex) {
    int braceLevel = 0;
    bool blockStarted = false;
    for (size_t i = startIndex; i < lines.size(); i++) {
        string currentLine = lines[i];
        for (char c : currentLine) {
            if (c == '{') {
                braceLevel++;
                blockStarted = true;
            } else if (c == '}') {
                braceLevel--;
            }
        }
        if (blockStarted && braceLevel <= 0)
            return i + 1;
    }
    return lines.size();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: trustc <filename>\n";
        return 1;
    }
    
    string inputFileName = argv[1];
    ifstream inFile(inputFileName);
    if (!inFile) {
        cerr << "Error: Cannot open file " << inputFileName << endl;
        return 1;
    }
    
    vector<string> lines;
    string line;
    while(getline(inFile, line)) {
        line = removeLineComments(line);
        lines.push_back(line);
    }
    
    unordered_map<string, double> variables;
    unordered_map<string, string> stringVariables;
    unordered_map<string, vector<ArrayElement>> arrays;
    vector<string> outputs;
    vector<int> outLengths;
    unordered_map<string, FunctionInfo> functions;
    int argCounter = 0;
    int lineNumber = 0;
    bool errorOccurred = false;
    
    for(size_t lineIndex = 0; lineIndex < lines.size(); lineIndex++) {
        lineNumber++;
        string origLine = lines[lineIndex];
        string lineTrimmed = trim(origLine);
        if (lineTrimmed.empty() || lineTrimmed == "}") {
            lineNumber--;
            continue;
        }
        if (lineTrimmed.rfind("Integer", 0) == 0) {
            string rest = trim(lineTrimmed.substr(7));
            size_t pos = 0;
            if(pos >= rest.size() || !(isalpha(rest[pos]) || rest[pos] == '_')) {
                reportError(lineNumber, origLine, "find valid variable name");
                errorOccurred = true;
                break;
            }
            size_t startIdent = pos;
            while(pos < rest.size() && (isalnum(rest[pos]) || rest[pos] == '_')) pos++;
            string varName = rest.substr(startIdent, pos - startIdent);
            rest = trim(rest.substr(pos));
            if(rest.empty() || rest[0] != '=') {
                reportError(lineNumber, origLine, "find '=' in variable declaration");
                errorOccurred = true;
                break;
            }
            rest = trim(rest.substr(1));
            if(rest.back() != ';') {
                reportError(lineNumber, origLine, "missing ';' at end of variable declaration");
                errorOccurred = true;
                break;
            }
            rest = trim(rest.substr(0, rest.size()-1));
            try {
                ExpressionParser parser(rest, variables, arrays, stringVariables);
                variables[varName] = parser.parse();
            } catch(exception &e) {
                reportError(lineNumber, origLine, e.what());
                errorOccurred = true;
                break;
            }
        }
        else if (lineTrimmed.rfind("String", 0) == 0) {
            string rest = trim(lineTrimmed.substr(6));
            size_t eqPos = rest.find('=');
            if(eqPos == string::npos) {
                reportError(lineNumber, origLine, "find '=' in String declaration");
                errorOccurred = true;
                break;
            }
            string varPart = trim(rest.substr(0, eqPos));
            string valuePart = trim(rest.substr(eqPos + 1));
            size_t endVar = varPart.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789");
            string varName = varPart.substr(0, endVar);
            if (valuePart.empty() || valuePart.back() != ';') {
                reportError(lineNumber, origLine, "missing ';' at end of String declaration");
                errorOccurred = true;
                break;
            }
            valuePart = valuePart.substr(0, valuePart.size()-1);
            if (valuePart.size() < 2 || valuePart.front() != '\"' || valuePart.back() != '\"') {
                reportError(lineNumber, origLine, "invalid string literal");
                errorOccurred = true;
                break;
            }
            stringVariables[varName] = valuePart.substr(1, valuePart.size() - 2);
        }
        else if (lineTrimmed.rfind("Bool", 0) == 0) {
            string rest = trim(lineTrimmed.substr(4));
            size_t eqPos = rest.find('=');
            if(eqPos == string::npos) {
                reportError(lineNumber, origLine, "find '=' in Bool declaration");
                errorOccurred = true;
                break;
            }
            string varPart = trim(rest.substr(0, eqPos));
            string exprPart = trim(rest.substr(eqPos + 1));
            if (exprPart.back() == ';') exprPart = exprPart.substr(0, exprPart.size() - 1);
            exprPart = trim(exprPart);
            double value;
            if (exprPart == "true") value = 1.0;
            else if (exprPart == "false") value = 0.0;
            else {
                reportError(lineNumber, origLine, "invalid boolean value");
                errorOccurred = true;
                break;
            }
            size_t endVar = varPart.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789");
            variables[varPart.substr(0, endVar)] = value;
        }
        else if (lineTrimmed.rfind("Array", 0) == 0) {
            string rest = trim(lineTrimmed.substr(5));
            size_t eqPos = rest.find('=');
            if (eqPos == string::npos) {
                reportError(lineNumber, origLine, "find '=' in Array declaration");
                errorOccurred = true;
                continue;
            }
            string varPart = trim(rest.substr(0, eqPos));
            if (varPart.empty() || !(isalpha(varPart[0]) || varPart[0] == '_')) {
                reportError(lineNumber, origLine, "invalid array name");
                errorOccurred = true;
                continue;
            }
            string arrayName = varPart;
            string elementsPart = trim(rest.substr(eqPos + 1));
            if (!elementsPart.empty() && elementsPart.back() == ';') elementsPart.pop_back();
            if (elementsPart.empty() || elementsPart.front() != '[' || elementsPart.back() != ']') {
                reportError(lineNumber, origLine, "array elements must be enclosed in []");
                errorOccurred = true;
                continue;
            }
            elementsPart = elementsPart.substr(1, elementsPart.size() - 2);
            vector<string> elements = splitArgs(elementsPart);
            vector<ArrayElement> arrayElements;
            for (string elemStr : elements) {
                elemStr = trim(elemStr);
                ArrayElement elem;
                if (elemStr.front() == '\"' && elemStr.back() == '\"') {
                    elem.type = ArrayElementType::STRING;
                    elem.stringValue = elemStr.substr(1, elemStr.size() - 2);
                } else if (elemStr == "true") {
                    elem.type = ArrayElementType::BOOL;
                    elem.numberValue = 1.0;
                } else if (elemStr == "false") {
                    elem.type = ArrayElementType::BOOL;
                    elem.numberValue = 0.0;
                } else {
                    try {
                        ExpressionParser parser(elemStr, variables, arrays, stringVariables);
                        elem.numberValue = parser.parse();
                        elem.type = ArrayElementType::NUMBER;
                    } catch (exception &e) {
                        reportError(lineNumber, origLine, e.what());
                        errorOccurred = true;
                        break;
                    }
                }
                arrayElements.push_back(elem);
            }
            if (!errorOccurred) arrays[arrayName] = arrayElements;
        }
        else if (lineTrimmed.rfind("Memory", 0) == 0) {
            string rest = trim(lineTrimmed.substr(6));
            size_t eqPos = rest.find('=');
            if(eqPos == string::npos) {
                reportError(lineNumber, origLine, "find '=' in function declaration");
                errorOccurred = true;
                break;
            }
            string funcDecl = trim(rest.substr(0, eqPos));
            string funcName;
            vector<pair<string, string>> params;
            size_t openParen = funcDecl.find('(');
            if(openParen != string::npos) {
                size_t closeParen = funcDecl.find(')', openParen);
                if(closeParen == string::npos) {
                    reportError(lineNumber, origLine, "missing closing parenthesis");
                    errorOccurred = true;
                    break;
                }
                funcName = trim(funcDecl.substr(0, openParen));
                string paramsStr = trim(funcDecl.substr(openParen+1, closeParen-openParen-1));
                vector<string> paramsList = splitArgs(paramsStr);
                for(string &param : paramsList) {
                    size_t spacePos = param.find_last_of(' ');
                    if(spacePos == string::npos) {
                        reportError(lineNumber, origLine, "invalid parameter syntax");
                        errorOccurred = true;
                        break;
                    }
                    string type = trim(param.substr(0, spacePos));
                    string name = trim(param.substr(spacePos+1));
                    params.emplace_back(type, name);
                }
            } else {
                funcName = funcDecl;
            }
            rest = trim(rest.substr(eqPos + 1));
            if(rest != "{") {
                reportError(lineNumber, origLine, "expected '{' after function declaration");
                errorOccurred = true;
                break;
            }
            size_t endBlock = skipBlock(lines, lineIndex);
            vector<string> funcBody;
            for(size_t i = lineIndex + 1; i < endBlock - 1; i++) {
                funcBody.push_back(lines[i]);
            }
            functions[funcName] = {funcBody, params};
            lineIndex = endBlock - 1;
        }
        else if (lineTrimmed.rfind("print", 0) == 0) {
            size_t pos = 5;
            while(pos < lineTrimmed.size() && isspace(lineTrimmed[pos])) pos++;
            if(pos >= lineTrimmed.size() || lineTrimmed[pos] != '(') {
                reportError(lineNumber, origLine, "find '(' after print");
                errorOccurred = true;
                break;
            }
            pos++;
            size_t argsStart = pos;
            size_t closingParen = lineTrimmed.find(')', pos);
            if(closingParen == string::npos) {
                reportError(lineNumber, origLine, "find matching ')'");
                errorOccurred = true;
                break;
            }
            string argsStr = trim(lineTrimmed.substr(argsStart, closingParen - argsStart));
            pos = closingParen + 1;
            while(pos < lineTrimmed.size() && isspace(lineTrimmed[pos])) pos++;
            if(pos >= lineTrimmed.size() || lineTrimmed[pos] != ';') {
                reportError(lineNumber, origLine, "find ';' at end of print call");
                errorOccurred = true;
                break;
            }
            vector<string> args = splitArgs(argsStr);
            if(args.size() != 1) {
                reportError(lineNumber, origLine, "print expects exactly one argument");
                errorOccurred = true;
                break;
            }
            string outputArg = args[0];
            string outStr;
            size_t bracketOpen = outputArg.find('[');
            size_t bracketClose = outputArg.find(']');
            if (bracketOpen != string::npos && bracketClose == outputArg.size() - 1) {
                string arrayName = outputArg.substr(0, bracketOpen);
                string indexExpr = outputArg.substr(bracketOpen + 1, bracketClose - bracketOpen - 1);
                try {
                    ExpressionParser indexParser(indexExpr, variables, arrays, stringVariables);
                    double index = indexParser.parse();
                    int idx = static_cast<int>(index);
                    if (!arrays.count(arrayName)) throw runtime_error("Array '" + arrayName + "' not found");
                    if (idx < 0 || idx >= arrays[arrayName].size()) throw runtime_error("Index out of bounds");
                    ArrayElement elem = arrays[arrayName][idx];
                    switch (elem.type) {
                        case ArrayElementType::NUMBER: outStr = formatNumber(elem.numberValue); break;
                        case ArrayElementType::BOOL: outStr = elem.numberValue ? "true" : "false"; break;
                        case ArrayElementType::STRING: outStr = elem.stringValue; break;
                    }
                } catch (exception &e) {
                    reportError(lineNumber, origLine, e.what());
                    errorOccurred = true;
                    break;
                }
            }
            else if (outputArg.size() >= 2 && outputArg.front() == '\"' && outputArg.back() == '\"') {
                outStr = outputArg.substr(1, outputArg.size() - 2);
            } else {
                auto strIt = stringVariables.find(outputArg);
                if (strIt != stringVariables.end()) {
                    outStr = strIt->second;
                } else {
                    try {
                        ExpressionParser parser(outputArg, variables, arrays, stringVariables);
                        double result = parser.parse();
                        outStr = formatNumber(result);
                    } catch(exception &e) {
                        reportError(lineNumber, origLine, e.what());
                        errorOccurred = true;
                        break;
                    }
                }
            }
            // Добавляем перенос строки, если его нет
            if(outStr.empty() || outStr.back() != '\n') {
                outStr.push_back('\n');
            }
            outputs.push_back(outStr);
            outLengths.push_back(outStr.size() + 1);
        }
        else if (lineTrimmed.find('(') != string::npos && ((lineTrimmed.back() == ')') || (lineTrimmed.back() == ';' && lineTrimmed[lineTrimmed.size()-2] == ')'))) {
            if(lineTrimmed.back() == ';')
                lineTrimmed = trim(lineTrimmed.substr(0, lineTrimmed.size()-1));
            size_t openParen = lineTrimmed.find('(');
            size_t closeParen = lineTrimmed.find(')');
            string funcName = trim(lineTrimmed.substr(0, openParen));
            if(!functions.count(funcName)) {
                reportError(lineNumber, origLine, "undefined function: " + funcName);
                errorOccurred = true;
                break;
            }
            FunctionInfo &func = functions[funcName];
            string argsStr = trim(lineTrimmed.substr(openParen+1, closeParen-openParen-1));
            vector<string> args = splitArgs(argsStr);
            if(args.size() != func.parameters.size()) {
                reportError(lineNumber, origLine, "wrong number of arguments for " + funcName);
                errorOccurred = true;
                break;
            }
            vector<string> llvmArgs;
            for(size_t i = 0; i < args.size(); i++) {
                auto &[type, name] = func.parameters[i];
                if(type == "String") {
                    if(args[i].size() < 2 || args[i].front() != '\"' || args[i].back() != '\"') {
                        reportError(lineNumber, origLine, "string literal expected for parameter " + name);
                        errorOccurred = true;
                        break;
                    }
                    string content = args[i].substr(1, args[i].size()-2);
                    string globalName = "argstr_" + to_string(argCounter++);
                    outputs.push_back("GLOBAL:" + globalName + ":" + content);
                    llvmArgs.push_back("i8* getelementptr inbounds ([" + to_string(content.size()+1) + 
                                    " x i8], [" + to_string(content.size()+1) + 
                                    " x i8]* @" + globalName + ", i32 0, i32 0)");
                }
            }
            if(errorOccurred) break;
            outputs.push_back("CALL:" + funcName + ":" + join(llvmArgs.begin(), llvmArgs.end(), ":"));
            outLengths.push_back(0);
        }
        else if (lineTrimmed.rfind("if", 0) == 0) {
            size_t openParen = lineTrimmed.find('(');
            size_t closeParen = lineTrimmed.find(')');
            if (openParen == string::npos || closeParen == string::npos) {
                reportError(lineNumber, origLine, "invalid if syntax");
                errorOccurred = true;
                break;
            }
            string conditionExpr = trim(lineTrimmed.substr(openParen + 1, closeParen - openParen - 1));
            try {
                ExpressionParser parser(conditionExpr, variables, arrays, stringVariables);
                double conditionValue = parser.parse();
                if (conditionValue != 1.0) {
                    lineIndex = skipBlock(lines, lineIndex) - 1;
                }
            } catch(exception& e) {
                reportError(lineNumber, origLine, e.what());
                errorOccurred = true;
                break;
            }
        }
        else {
            reportError(lineNumber, origLine, "unrecognized statement");
            errorOccurred = true;
            break;
        }
    }
    
    if(errorOccurred) return 1;
    
    fs::path tmpDir = "tmp";
    try { fs::create_directory(tmpDir); } 
    catch(exception &e) { cerr << "Error creating tmp directory: " << e.what() << endl; return 1; }
    
    ostringstream llvmIR;
    llvmIR << "; ModuleID = 'trust'\n"
           << "declare i32 @printf(i8*, ...)\n\n";
    
    // Собираем все глобальные определения строк, только если длина больше 0
    vector<string> allGlobalStrings;
    for (size_t i = 0; i < outputs.size(); i++) {
        if(outputs[i].rfind("GLOBAL:", 0) == 0) {
            size_t pos1 = outputs[i].find(':', 7);
            string name = outputs[i].substr(7, pos1-7);
            string content = outputs[i].substr(pos1+1);
            allGlobalStrings.push_back(llvmGlobalString(content, name, content.size()+1));
        }
        else if(!outputs[i].empty() && outputs[i][0] != 'G' && outLengths[i] > 0) {
            string globalName = "str." + to_string(i);
            allGlobalStrings.push_back(llvmGlobalString(outputs[i], globalName, outLengths[i]));
        }
    }
    
    // Генерация кода функций
    ostringstream functionsIR;
    for (auto &[funcName, funcInfo] : functions) {
        ostringstream funcIR;
        funcIR << "define void @" << funcName << "(";
        vector<string> params;
        for(auto &[type, name] : funcInfo.parameters) {
            if(type == "String") {
                params.push_back("i8* %" + name);
            }
        }
        funcIR << join(params.begin(), params.end(), ", ") << ") {\nentry:\n";
        for(string &fline : funcInfo.body) {
            string ftrim = trim(fline);
            if(ftrim.rfind("print", 0) == 0) {
                size_t openParen = ftrim.find('(');
                size_t closeParen = ftrim.find(')');
                string arg = trim(ftrim.substr(openParen+1, closeParen-openParen-1));
                bool isParam = false;
                for(auto &[type, name] : funcInfo.parameters) {
                    if(arg == name) {
                        funcIR << "  call i32 (i8*, ...) @printf(i8* %" << name << ")\n";
                        isParam = true;
                        break;
                    }
                }
                if(!isParam) {
                    if(arg.size() >= 2 && arg.front() == '\"' && arg.back() == '\"') {
                        string content = arg.substr(1, arg.size()-2);
                        // Добавляем перенос строки, если его нет
                        if(content.empty() || content.back() != '\n') {
                            content.push_back('\n');
                        }
                        string globalName = "fstr_" + funcName + "_" + to_string(argCounter++);
                        allGlobalStrings.push_back(llvmGlobalString(content, globalName, content.size()+1));
                        funcIR << "  call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([" 
                               << content.size()+1 << " x i8], [" << content.size()+1 << " x i8]* @" 
                               << globalName << ", i32 0, i32 0))\n";
                    }
                }
            }
        }
        funcIR << "  ret void\n}\n";
        functionsIR << funcIR.str();
    }
    
    // Выводим все глобальные определения на уровне модуля
    for(auto &globalDef : allGlobalStrings) {
        llvmIR << globalDef << "\n";
    }
    
    // Функция main
    llvmIR << "\ndefine i32 @main() {\nentry:\n";
    for (size_t i = 0; i < outputs.size(); i++) {
        if(outputs[i].rfind("CALL:", 0) == 0) {
            size_t pos1 = outputs[i].find(':', 5);
            string funcName = outputs[i].substr(5, pos1-5);
            string args = outputs[i].substr(pos1+1);
            replace(args.begin(), args.end(), ':', ',');
            llvmIR << "  call void @" << funcName << "(" << args << ")\n";
        }
        else if(!outputs[i].empty() && outputs[i][0] != 'G' && outLengths[i] > 0) {
            string globalName = "str." + to_string(i);
            llvmIR << "  call i32 (i8*, ...) @printf(i8* getelementptr inbounds (["
                   << outLengths[i] << " x i8], ["
                   << outLengths[i] << " x i8]* @" << globalName << ", i32 0, i32 0))\n";
        }
    }
    llvmIR << "  ret i32 0\n}\n";
    
    // Добавляем сгенерированные функции
    llvmIR << functionsIR.str();
    
    fs::path llvmFile = tmpDir / "output.ll";
    ofstream outLL(llvmFile);
    if(!outLL) {
        cerr << "Error: Cannot open file " << llvmFile << " for writing." << endl;
        return 1;
    }
    outLL << llvmIR.str();
    outLL.close();
    
    fs::path binPath = fs::path(inputFileName).parent_path() / fs::path(inputFileName).stem();
    string compileCmd = "clang -Wno-override-module " + llvmFile.string() + " -o " + binPath.string();
    if (system(compileCmd.c_str()) != 0) {
        cerr << "Compilation with clang failed." << endl;
        return 1;
    }
    
    try { fs::remove_all(tmpDir); }
    catch(exception &e) { cerr << "Error removing tmp directory: " << e.what() << endl; }
    
    return 0;
}
