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
string join(Iterator begin, Iterator end, const string &separator) {
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
    vector<pair<string, string>> parameters; // {тип, имя}
};

string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if(start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

string removeLineComments(const string &line) {
    bool inQuote = false;
    for (size_t i = 0; i < line.size(); i++) {
        if(line[i]=='\"'){
            if(i==0 || line[i-1] != '\\')
                inQuote = !inQuote;
        }
        if(!inQuote && i+1 < line.size() && line[i]=='/' && line[i+1]=='/')
            return line.substr(0, i);
    }
    return line;
}

vector<string> splitArgs(const string &argsStr) {
    vector<string> args;
    string current;
    bool inQuotes = false;
    for(char c : argsStr) {
        if(c=='\"'){
            inQuotes = !inQuotes;
            current.push_back(c);
        } else if(c==',' && !inQuotes) {
            args.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if(!current.empty())
        args.push_back(trim(current));
    return args;
}

void reportError(int lineNumber, const string &origLine, const string &description) {
    cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\nBuilding failed on line " 
         << lineNumber << ":\n  \"" << origLine 
         << "\" - " << description << "\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << endl;
}

// Функция для экранирования символов '%' в строке (замена на "%%")
string escapePercent(const string &input) {
    string result;
    for (char c : input) {
        if(c == '%')
            result += "%%";
        else
            result.push_back(c);
    }
    return result;
}

class ExpressionParser {
public:
    ExpressionParser(const string &expr, 
                     const unordered_map<string, double> &vars)
        : s(expr), pos(0), variables(vars) {}
    
    double parse() {
        double result = parseExpression();
        skipSpaces();
        if(pos != s.size())
            throw runtime_error("Unexpected characters at end of expression");
        return result;
    }
private:
    string s;
    size_t pos;
    const unordered_map<string, double>& variables;
    
    void skipSpaces() {
        while(pos < s.size() && isspace(s[pos])) pos++;
    }
    
    double parseExpression() {
        double result = parseTerm();
        while(true) {
            skipSpaces();
            if(pos < s.size() && (s[pos]=='+' || s[pos]=='-')) {
                char op = s[pos++];
                double term = parseTerm();
                result = (op=='+') ? result + term : result - term;
            } else break;
        }
        return result;
    }
    
    double parseTerm() {
        double result = parseFactor();
        while(true) {
            skipSpaces();
            if(pos < s.size() && (s[pos]=='*' || s[pos]=='/')) {
                char op = s[pos++];
                double factor = parseFactor();
                result = (op=='*') ? result * factor : result / factor;
            } else break;
        }
        return result;
    }
    
    double parseFactor() {
        skipSpaces();
        if(pos < s.size() && s[pos]=='!') {
            pos++;
            double value = parseFactor();
            return (value==0.0) ? 1.0 : 0.0;
        }
        if(pos < s.size() && s[pos]=='(') {
            pos++;
            double result = parseExpression();
            skipSpaces();
            if(pos >= s.size() || s[pos]!=')')
                throw runtime_error("Missing closing parenthesis");
            pos++;
            return result;
        }
        if(pos < s.size() && (isalpha(s[pos]) || s[pos]=='_')) {
            return parseIdentifier();
        }
        return parseNumber();
    }
    
    double parseIdentifier() {
        size_t start = pos;
        while(pos < s.size() && (isalnum(s[pos]) || s[pos]=='_'))
            pos++;
        string ident = s.substr(start, pos - start);
        if(ident=="true") return 1.0;
        if(ident=="false") return 0.0;
        auto it = variables.find(ident);
        if(it != variables.end())
            return it->second;
        throw runtime_error("Undefined variable: " + ident);
    }
    
    double parseNumber() {
        skipSpaces();
        size_t start = pos;
        if(pos < s.size() && (s[pos]=='+' || s[pos]=='-'))
            pos++;
        bool dotFound = false;
        while(pos < s.size() && (isdigit(s[pos]) || s[pos]=='.')) {
            if(s[pos]=='.'){
                if(dotFound) break;
                dotFound = true;
            }
            pos++;
        }
        if(start==pos)
            throw runtime_error("Expected number");
        return stod(s.substr(start, pos-start));
    }
};

string formatNumber(double num) {
    ostringstream oss;
    if(fabs(num - round(num)) < 1e-9)
        oss << static_cast<long long>(round(num));
    else
        oss << num;
    return oss.str();
}

string llvmGlobalString(const string &str, const string &name, int actualLen) {
    string escaped;
    for(char c : str) {
        if(c=='\\')
            escaped += "\\5C";
        else if(c=='\"')
            escaped += "\\22";
        else if(c=='\n')
            escaped += "\\0A";
        else if(c=='\r')
            escaped += "\\0D";
        else if(c < 32 || c > 126) {
            char buf[5];
            snprintf(buf, sizeof(buf), "\\%02X", static_cast<unsigned char>(c));
            escaped += buf;
        } else
            escaped.push_back(c);
    }
    return "@" + name + " = private constant [" + to_string(actualLen) + " x i8] c\"" + escaped + "\\00\"";
}

size_t skipBlock(const vector<string> &lines, size_t startIndex) {
    int braceLevel = 0;
    bool blockStarted = false;
    for(size_t i = startIndex; i < lines.size(); i++){
        string currentLine = lines[i];
        for(char c : currentLine) {
            if(c=='{'){
                braceLevel++;
                blockStarted = true;
            } else if(c=='}'){
                braceLevel--;
            }
        }
        if(blockStarted && braceLevel <= 0)
            return i + 1;
    }
    return lines.size();
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        cerr << "Usage: trustc <filename>\n";
        return 1;
    }
    
    string inputFileName = argv[1];
    ifstream inFile(inputFileName);
    if(!inFile) {
        cerr << "Error: Cannot open file " << inputFileName << endl;
        return 1;
    }
    
    vector<string> lines;
    string line;
    while(getline(inFile, line)) {
        line = removeLineComments(line);
        lines.push_back(line);
    }
    
    unordered_map<string, double> globalVariables;
    unordered_map<string, string> globalStrings;
    unordered_map<string, vector<ArrayElement>> arrays;
    // Глобальные инструкции: для print – строки, для вызовов функций – маркеры
    vector<pair<bool, string>> globalInstructions; // {isCall, content}
    vector<int> instrLengths;
    
    unordered_map<string, FunctionInfo> functions;
    
    int argCounter = 0;
    int lineNumber = 0;
    bool errorOccurred = false;
    
    // Вектор для глобальных определений строк (например, для аргументов)
    vector<string> allGlobalStrings;
    
    // Глобальный проход: разбор инструкций и определений
    for(size_t i = 0; i < lines.size(); i++){
        lineNumber++;
        string origLine = lines[i];
        string lineTrimmed = trim(origLine);
        if(lineTrimmed.empty() || lineTrimmed == "}") {
            lineNumber--;
            continue;
        }
        if(lineTrimmed.rfind("Integer", 0) == 0) {
            string rest = trim(lineTrimmed.substr(7));
            size_t pos = 0;
            if(pos >= rest.size() || !(isalpha(rest[pos]) || rest[pos]=='_')) {
                reportError(lineNumber, origLine, "invalid variable name");
                errorOccurred = true;
                break;
            }
            size_t startIdent = pos;
            while(pos < rest.size() && (isalnum(rest[pos]) || rest[pos]=='_'))
                pos++;
            string varName = rest.substr(startIdent, pos - startIdent);
            rest = trim(rest.substr(pos));
            if(rest.empty() || rest[0] != '=') {
                reportError(lineNumber, origLine, "expected '=' in declaration");
                errorOccurred = true;
                break;
            }
            rest = trim(rest.substr(1));
            if(rest.back() != ';') {
                reportError(lineNumber, origLine, "missing ';' at end");
                errorOccurred = true;
                break;
            }
            rest = trim(rest.substr(0, rest.size()-1));
            try {
                ExpressionParser parser(rest, globalVariables);
                globalVariables[varName] = parser.parse();
            } catch(exception &e) {
                reportError(lineNumber, origLine, e.what());
                errorOccurred = true;
                break;
            }
        }
        else if(lineTrimmed.rfind("String", 0) == 0) {
            string rest = trim(lineTrimmed.substr(6));
            size_t eqPos = rest.find('=');
            if(eqPos == string::npos) {
                reportError(lineNumber, origLine, "expected '=' in String declaration");
                errorOccurred = true;
                break;
            }
            string varPart = trim(rest.substr(0, eqPos));
            string valuePart = trim(rest.substr(eqPos+1));
            size_t endVar = varPart.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789");
            string varName = varPart.substr(0, endVar);
            if(valuePart.empty() || valuePart.back() != ';') {
                reportError(lineNumber, origLine, "missing ';' at end of String declaration");
                errorOccurred = true;
                break;
            }
            valuePart = valuePart.substr(0, valuePart.size()-1);
            if(valuePart.size() < 2 || valuePart.front() != '\"' || valuePart.back() != '\"') {
                reportError(lineNumber, origLine, "invalid string literal");
                errorOccurred = true;
                break;
            }
            globalStrings[varName] = valuePart.substr(1, valuePart.size()-2);
        }
        else if(lineTrimmed.rfind("Bool", 0) == 0) {
            string rest = trim(lineTrimmed.substr(4));
            size_t eqPos = rest.find('=');
            if(eqPos == string::npos) {
                reportError(lineNumber, origLine, "expected '=' in Bool declaration");
                errorOccurred = true;
                break;
            }
            string varPart = trim(rest.substr(0, eqPos));
            string exprPart = trim(rest.substr(eqPos+1));
            if(exprPart.back() == ';')
                exprPart = exprPart.substr(0, exprPart.size()-1);
            exprPart = trim(exprPart);
            double value;
            if(exprPart=="true") value = 1.0;
            else if(exprPart=="false") value = 0.0;
            else {
                reportError(lineNumber, origLine, "invalid boolean value");
                errorOccurred = true;
                break;
            }
            size_t endVar = varPart.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789");
            globalVariables[varPart.substr(0, endVar)] = value;
        }
        else if(lineTrimmed.rfind("Array", 0) == 0) {
            string rest = trim(lineTrimmed.substr(5));
            size_t eqPos = rest.find('=');
            if(eqPos == string::npos) {
                reportError(lineNumber, origLine, "expected '=' in Array declaration");
                errorOccurred = true;
                break;
            }
            string varPart = trim(rest.substr(0, eqPos));
            if(varPart.empty() || !(isalpha(varPart[0]) || varPart[0]=='_')) {
                reportError(lineNumber, origLine, "invalid array name");
                errorOccurred = true;
                break;
            }
            string arrayName = varPart;
            string elementsPart = trim(rest.substr(eqPos+1));
            if(!elementsPart.empty() && elementsPart.back() == ';')
                elementsPart.pop_back();
            if(elementsPart.empty() || elementsPart.front() != '[' || elementsPart.back() != ']') {
                reportError(lineNumber, origLine, "array elements must be in []");
                errorOccurred = true;
                break;
            }
            elementsPart = elementsPart.substr(1, elementsPart.size()-2);
            vector<string> elements = splitArgs(elementsPart);
            vector<ArrayElement> arrayElements;
            for(string elemStr : elements) {
                elemStr = trim(elemStr);
                ArrayElement elem;
                if(elemStr.front()=='\"' && elemStr.back()=='\"'){
                    elem.type = ArrayElementType::STRING;
                    elem.stringValue = elemStr.substr(1, elemStr.size()-2);
                } else if(elemStr=="true"){
                    elem.type = ArrayElementType::BOOL;
                    elem.numberValue = 1.0;
                } else if(elemStr=="false"){
                    elem.type = ArrayElementType::BOOL;
                    elem.numberValue = 0.0;
                } else {
                    try {
                        ExpressionParser parser(elemStr, globalVariables);
                        elem.numberValue = parser.parse();
                        elem.type = ArrayElementType::NUMBER;
                    } catch(exception &e){
                        reportError(lineNumber, origLine, e.what());
                        errorOccurred = true;
                        break;
                    }
                }
                arrayElements.push_back(elem);
            }
            if(errorOccurred) break;
            arrays[arrayName] = arrayElements;
        }
        else if(lineTrimmed.rfind("Memory", 0) == 0) {
            string rest = trim(lineTrimmed.substr(6));
            size_t eqPos = rest.find('=');
            if(eqPos == string::npos) {
                reportError(lineNumber, origLine, "expected '=' in function declaration");
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
                for(auto &p : paramsList) {
                    size_t spacePos = p.find_last_of(' ');
                    if(spacePos == string::npos) {
                        reportError(lineNumber, origLine, "invalid parameter syntax");
                        errorOccurred = true;
                        break;
                    }
                    string type = trim(p.substr(0, spacePos));
                    string name = trim(p.substr(spacePos+1));
                    params.emplace_back(type, name);
                }
            } else {
                funcName = funcDecl;
            }
            rest = trim(rest.substr(eqPos+1));
            if(rest != "{") {
                reportError(lineNumber, origLine, "expected '{' after function declaration");
                errorOccurred = true;
                break;
            }
            size_t endBlock = skipBlock(lines, i);
            vector<string> funcBody;
            for(size_t j = i+1; j < endBlock-1; j++){
                funcBody.push_back(lines[j]);
            }
            functions[funcName] = {funcBody, params};
            i = endBlock - 1;
        }
        // Сначала проверяем, является ли строка оператором print
        else if(lineTrimmed.rfind("print", 0) == 0) {
            size_t pos = 5;
            while(pos < lineTrimmed.size() && isspace(lineTrimmed[pos])) pos++;
            if(pos >= lineTrimmed.size() || lineTrimmed[pos] != '(') {
                reportError(lineNumber, origLine, "expected '(' after print");
                errorOccurred = true;
                break;
            }
            pos++;
            size_t closingParen = lineTrimmed.find(')', pos);
            if(closingParen == string::npos) {
                reportError(lineNumber, origLine, "expected matching ')'");
                errorOccurred = true;
                break;
            }
            string argExpr = trim(lineTrimmed.substr(pos, closingParen-pos));
            pos = closingParen+1;
            while(pos < lineTrimmed.size() && isspace(lineTrimmed[pos])) pos++;
            if(pos >= lineTrimmed.size() || lineTrimmed[pos] != ';') {
                reportError(lineNumber, origLine, "expected ';' at end of print");
                errorOccurred = true;
                break;
            }
            // Если аргумент – строковый литерал, экранируем символы '%'
            if(argExpr.size() >= 2 && argExpr.front()=='\"' && argExpr.back()=='\"') {
                string outStr = argExpr.substr(1, argExpr.size()-2);
                outStr = escapePercent(outStr);
                if(outStr.empty() || outStr.back()!='\n')
                    outStr.push_back('\n');
                globalInstructions.push_back({false, outStr});
                instrLengths.push_back(outStr.size()+1);
            } else {
                try {
                    ExpressionParser parser(argExpr, globalVariables);
                    double value = parser.parse();
                    string outStr = formatNumber(value);
                    if(outStr.empty() || outStr.back()!='\n')
                        outStr.push_back('\n');
                    globalInstructions.push_back({false, outStr});
                    instrLengths.push_back(outStr.size()+1);
                } catch(exception &e) {
                    reportError(lineNumber, origLine, e.what());
                    errorOccurred = true;
                    break;
                }
            }
        }
        // Затем проверяем вызов функции
        else if(lineTrimmed.find('(') != string::npos &&
                (lineTrimmed.back() == ')' || (lineTrimmed.back() == ';' && lineTrimmed[lineTrimmed.size()-2] == ')'))) {
            string callLine = lineTrimmed;
            if(callLine.back() == ';')
                callLine = trim(callLine.substr(0, callLine.size()-1));
            size_t openParen = callLine.find('(');
            size_t closeParen = callLine.find(')', openParen);
            string funcName = trim(callLine.substr(0, openParen));
            if(!functions.count(funcName)) {
                reportError(lineNumber, origLine, "undefined function: " + funcName);
                errorOccurred = true;
                break;
            }
            FunctionInfo &callee = functions[funcName];
            string argsStr = trim(callLine.substr(openParen+1, closeParen-openParen-1));
            vector<string> argsList = splitArgs(argsStr);
            if(argsList.size() != callee.parameters.size()){
                reportError(lineNumber, origLine, "wrong number of arguments for " + funcName);
                errorOccurred = true;
                break;
            }
            ostringstream marker;
            marker << "CALL:" << funcName;
            for(size_t j = 0; j < argsList.size(); j++){
                auto &param = callee.parameters[j];
                if(param.first == "String"){
                    if(argsList[j].size() < 2 || argsList[j].front() != '\"' || argsList[j].back() != '\"'){
                        reportError(lineNumber, origLine, "string literal expected for parameter " + param.second);
                        errorOccurred = true;
                        break;
                    }
                    string content = argsList[j].substr(1, argsList[j].size()-2);
                    string globalName = "argstr_" + to_string(argCounter++);
                    allGlobalStrings.push_back(llvmGlobalString(content, globalName, content.size()+1));
                    marker << ":" << "i8* getelementptr inbounds (["
                           << content.size()+1 << " x i8], [" << content.size()+1
                           << " x i8]* @" << globalName << ", i32 0, i32 0)";
                } else if(param.first=="Integer"){
                    unordered_map<string, double> tempVars = globalVariables;
                    ExpressionParser parser(argsList[j], tempVars);
                    double value = parser.parse();
                    marker << ":" << "i32 " << to_string(static_cast<int>(value));
                }
            }
            globalInstructions.push_back({true, marker.str()});
            instrLengths.push_back(0);
        }
        else {
            reportError(lineNumber, origLine, "unrecognized statement");
            errorOccurred = true;
            break;
        }
    }
    
    if(errorOccurred)
        return 1;
    
    // Подготовка для генерации LLVM IR
    fs::path tmpDir = "tmp";
    try { fs::create_directory(tmpDir); }
    catch(exception &e) { cerr << "Error creating tmp directory: " << e.what() << endl; return 1; }
    
    ostringstream llvmIR;
    llvmIR << "; ModuleID = 'trust'\n"
           << "declare i32 @printf(i8*, ...)\n\n";
    
    // Генерация глобальных строк для print-инструкций
    int printIndex = 0;
    for(size_t i = 0; i < globalInstructions.size(); i++){
        if(!globalInstructions[i].first) { // обычный print
            string globalName = "str." + to_string(i);
            llvmIR << llvmGlobalString(globalInstructions[i].second, globalName, instrLengths[printIndex]) << "\n";
            printIndex++;
        }
    }
    
    // Генерация функций
    ostringstream functionsIR;
    for(auto &funcPair : functions) {
        string funcName = funcPair.first;
        FunctionInfo &funcInfo = funcPair.second;
        ostringstream funcIR;
        funcIR << "define void @" << funcName << "(";
        vector<string> paramIR;
        for(auto &p : funcInfo.parameters) {
            if(p.first == "String")
                paramIR.push_back("i8* %" + p.second);
            else if(p.first == "Integer")
                paramIR.push_back("i32 %" + p.second);
            else if(p.first == "Bool")
                paramIR.push_back("i1 %" + p.second);
        }
        funcIR << join(paramIR.begin(), paramIR.end(), ", ") << ") {\nentry:\n";
        
        // Создаем локальную таблицу переменных и регистрируем параметры
        unordered_map<string, double> localVariables;
        for(auto &p : funcInfo.parameters) {
            if(p.first == "Integer")
                localVariables[p.second] = 0; // фиктивное значение
        }
        
        for(auto &fline : funcInfo.body) {
            string ftrim = trim(fline);
            if(ftrim.empty()) continue;
            if(ftrim.rfind("Integer", 0) == 0) {
                string rest = trim(ftrim.substr(7));
                size_t pos = 0;
                while(pos < rest.size() && isspace(rest[pos])) pos++;
                size_t startIdent = pos;
                while(pos < rest.size() && (isalnum(rest[pos]) || rest[pos]=='_')) pos++;
                string varName = rest.substr(startIdent, pos - startIdent);
                rest = trim(rest.substr(pos));
                if(rest.empty() || rest[0] != '=') {
                    reportError(lineNumber, fline, "expected '=' in local declaration");
                    errorOccurred = true;
                    break;
                }
                rest = trim(rest.substr(1));
                if(rest.back() != ';') {
                    reportError(lineNumber, fline, "missing ';' in local declaration");
                    errorOccurred = true;
                    break;
                }
                rest = trim(rest.substr(0, rest.size()-1));
                try {
                    ExpressionParser parser(rest, localVariables);
                    double val = parser.parse();
                    localVariables[varName] = val;
                } catch(exception &e) {
                    reportError(lineNumber, fline, e.what());
                    errorOccurred = true;
                    break;
                }
            }
            else if(ftrim.rfind("print", 0) == 0) {
                size_t pos = 5;
                while(pos < ftrim.size() && isspace(ftrim[pos])) pos++;
                if(pos >= ftrim.size() || ftrim[pos] != '(') {
                    reportError(lineNumber, fline, "expected '(' after print");
                    errorOccurred = true;
                    break;
                }
                pos++;
                size_t closingParen = ftrim.find(')', pos);
                if(closingParen == string::npos) {
                    reportError(lineNumber, fline, "expected ')' in print");
                    errorOccurred = true;
                    break;
                }
                string argExpr = trim(ftrim.substr(pos, closingParen-pos));
                bool isParam = false;
                for(auto &p : funcInfo.parameters) {
                    if(argExpr == p.second) {
                        if(p.first == "String") {
                            funcIR << "  call i32 (i8*, ...) @printf(i8* %" << p.second << ")\n";
                        } else if(p.first == "Integer") {
                            string numFormatStr = "fmt_" + funcName + "_" + to_string(argCounter++);
                            allGlobalStrings.push_back(llvmGlobalString("%d\n", numFormatStr, 4));
                            funcIR << "  call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @"
                                   << numFormatStr << ", i32 0, i32 0), i32 %" << p.second << ")\n";
                        }
                        isParam = true;
                        break;
                    }
                }
                if(!isParam) {
                    try {
                        unordered_map<string, double> tempVars = globalVariables;
                        for(auto &lv : localVariables)
                            tempVars[lv.first] = lv.second;
                        ExpressionParser parser(argExpr, tempVars);
                        double value = parser.parse();
                        string numStr = formatNumber(value);
                        if(numStr.empty() || numStr.back() != '\n')
                            numStr.push_back('\n');
                        string numFormatStr = "fmt_" + funcName + "_" + to_string(argCounter++);
                        allGlobalStrings.push_back(llvmGlobalString("%d\n", numFormatStr, 4));
                        funcIR << "  call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @"
                               << numFormatStr << ", i32 0, i32 0), i32 " << static_cast<int>(value) << ")\n";
                    } catch(exception &e) {
                        reportError(lineNumber, fline, e.what());
                        errorOccurred = true;
                        break;
                    }
                }
            }
            else if(ftrim.find('(') != string::npos &&
                    (ftrim.back() == ')' || (ftrim.back() == ';' && ftrim[ftrim.size()-2] == ')'))) {
                string callLine = ftrim;
                if(callLine.back() == ';')
                    callLine = trim(callLine.substr(0, callLine.size()-1));
                size_t openParen = callLine.find('(');
                size_t closeParen = callLine.find(')', openParen);
                string calledFunc = trim(callLine.substr(0, openParen));
                if(!functions.count(calledFunc)) {
                    reportError(lineNumber, fline, "undefined function: " + calledFunc);
                    errorOccurred = true;
                    break;
                }
                FunctionInfo &callee = functions[calledFunc];
                string argsStr = trim(callLine.substr(openParen+1, closeParen-openParen-1));
                vector<string> argsList = splitArgs(argsStr);
                if(argsList.size() != callee.parameters.size()){
                    reportError(lineNumber, fline, "wrong number of arguments for " + calledFunc);
                    errorOccurred = true;
                    break;
                }
                vector<string> llvmArgs;
                for(size_t i = 0; i < argsList.size(); i++){
                    auto &param = callee.parameters[i];
                    if(param.first=="String"){
                        if(argsList[i].size() < 2 || argsList[i].front()!='\"' || argsList[i].back()!='\"'){
                            reportError(lineNumber, fline, "string literal expected for parameter " + param.second);
                            errorOccurred = true;
                            break;
                        }
                        string content = argsList[i].substr(1, argsList[i].size()-2);
                        string globalName = "argstr_" + to_string(argCounter++);
                        allGlobalStrings.push_back(llvmGlobalString(content, globalName, content.size()+1));
                        llvmArgs.push_back("i8* getelementptr inbounds ([" + to_string(content.size()+1) +
                                           " x i8], [" + to_string(content.size()+1) +
                                           " x i8]* @" + globalName + ", i32 0, i32 0)");
                    } else if(param.first=="Integer"){
                        unordered_map<string, double> tempVars = globalVariables;
                        for(auto &lv : localVariables)
                            tempVars[lv.first] = lv.second;
                        ExpressionParser parser(argsList[i], tempVars);
                        double value = parser.parse();
                        llvmArgs.push_back("i32 " + to_string(static_cast<int>(value)));
                    }
                }
                if(errorOccurred)
                    break;
                funcIR << "  call void @" << calledFunc << "(" << join(llvmArgs.begin(), llvmArgs.end(), ", ") << ")\n";
            }
            else {
                reportError(lineNumber, fline, "unrecognized statement in function body");
                errorOccurred = true;
                break;
            }
        }
        funcIR << "  ret void\n}\n";
        functionsIR << funcIR.str();
    }
    
    llvmIR << functionsIR.str();
    
    // Генерация функции main()
    llvmIR << "\ndefine i32 @main() {\nentry:\n";
    printIndex = 0;
    for(auto &instr : globalInstructions) {
        if(instr.first) {
            vector<string> parts;
            istringstream iss(instr.second);
            string token;
            while(getline(iss, token, ':'))
                parts.push_back(token);
            if(parts.size() < 2) continue;
            string funcName = parts[1];
            vector<string> llvmArgs;
            for(size_t i = 2; i < parts.size(); i++){
                llvmArgs.push_back(parts[i]);
            }
            llvmIR << "  call void @" << funcName << "(" << join(llvmArgs.begin(), llvmArgs.end(), ", ") << ")\n";
        } else {
            string globalName = "str." + to_string(&instr - &globalInstructions[0]);
            llvmIR << "  call i32 (i8*, ...) @printf(i8* getelementptr inbounds (["
                  << instrLengths[printIndex] << " x i8], ["
                  << instrLengths[printIndex] << " x i8]* @" << globalName << ", i32 0, i32 0))\n";
            printIndex++;
        }
    }
    llvmIR << "  ret i32 0\n}\n";
    
    for(auto &globalDef : allGlobalStrings)
        llvmIR << globalDef << "\n";
    
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
    if(system(compileCmd.c_str()) != 0) {
        cerr << "Compilation with clang failed." << endl;
        return 1;
    }
    
    try { fs::remove_all(tmpDir); }
    catch(exception &e) { cerr << "Error removing tmp directory: " << e.what() << endl; }
    
    return 0;
}
