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

string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
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
    if (!current.empty()) {
        args.push_back(trim(current));
    }
    return args;
}

void reportError(int lineNumber, const string &origLine, const string &description) {
    cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << endl;
    cerr << "Building failed on " << lineNumber << " line:" << endl;
    cerr << "  \"" << origLine << "\" - unable to " << description << endl;
    cerr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << endl;
}

class ExpressionParser {
public:
    ExpressionParser(const string &expr, const unordered_map<string, double> &vars)
        : s(expr), pos(0), variables(vars) {}

    double parse() {
        double result = parseExpression();
        skipSpaces();
        if (pos != s.size())
            throw runtime_error("Unexpected characters at end of expression");
        return result;
    }

private:
    string s;
    size_t pos;
    const unordered_map<string, double>& variables;

    void skipSpaces() {
        while (pos < s.size() && isspace(s[pos])) pos++;
    }

    double parseIdentifier() {
        size_t start = pos;
        while (pos < s.size() && (isalnum(s[pos]) || s[pos]=='_'))
            pos++;
        string ident = s.substr(start, pos - start);
        auto it = variables.find(ident);
        if (it == variables.end())
            throw runtime_error("Undefined variable: " + ident);
        return it->second;
    }

    double parseExpression() {
        double result = parseTerm();
        while (true) {
            skipSpaces();
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
                char op = s[pos++];
                double term = parseTerm();
                result = (op == '+') ? result + term : result - term;
            } else {
                break;
            }
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
            } else {
                break;
            }
        }
        return result;
    }

    double parseFactor() {
        skipSpaces();
        if (pos < s.size() && s[pos] == '(') {
            pos++;
            double result = parseExpression();
            skipSpaces();
            if (pos >= s.size() || s[pos] != ')')
                throw runtime_error("Missing closing parenthesis");
            pos++;
            return result;
        }
        if (pos < s.size() && (isalpha(s[pos]) || s[pos]=='_')) {
            return parseIdentifier();
        }
        return parseNumber();
    }

    double parseNumber() {
        skipSpaces();
        size_t start = pos;
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-'))
            pos++;
        bool dotFound = false;
        while (pos < s.size() && (isdigit(s[pos]) || s[pos]=='.')) {
            if (s[pos]=='.') {
                if (dotFound)
                    break;
                dotFound = true;
            }
            pos++;
        }
        if (start == pos)
            throw runtime_error("Expected number");
        return stod(s.substr(start, pos - start));
    }
};

double evaluateExpression(const string &expr, const unordered_map<string, double> &vars) {
    ExpressionParser parser(expr, vars);
    return parser.parse();
}

string formatNumber(double num) {
    ostringstream oss;
    if (fabs(num - round(num)) < 1e-9)
        oss << static_cast<long long>(round(num));
    else
        oss << num;
    return oss.str();
}

string llvmGlobalString(const string &str, const string &name, int actualLen) {
    string escaped;
    for (char c : str) {
        if (c == '\\')
            escaped += "\\5C";
        else if (c == '\"')
            escaped += "\\22";
        else if (c == '\n')
            escaped += "\\0A";
        else if (c == '\r')
            escaped += "\\0D";
        else if (c < 32 || c > 126) {
            char buf[5];
            snprintf(buf, sizeof(buf), "\\%02X", static_cast<unsigned char>(c));
            escaped += buf;
        } else {
            escaped.push_back(c);
        }
    }
    escaped += "\\0A\\00";
    return "@" + name + " = private constant [" + to_string(actualLen) + " x i8] c\"" + escaped + "\"";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: trustc <filename>" << endl;
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
        lines.push_back(line);
    }
    
    unordered_map<string, double> variables;
    unordered_map<string, string> stringVariables;
    vector<string> outputs;
    vector<int> outLengths;
    
    int lineNumber = 0;
    bool errorOccurred = false;
    
    for(size_t lineIndex = 0; lineIndex < lines.size(); lineIndex++) {
        lineNumber++;
        string origLine = lines[lineIndex];
        string lineTrimmed = trim(origLine);
        if(lineTrimmed.empty())
            continue;
        
        if(lineTrimmed.rfind("Integer", 0) == 0) {
            string rest = trim(lineTrimmed.substr(7));
            size_t pos = 0;
            if(pos >= rest.size() || !(isalpha(rest[pos]) || rest[pos]=='_')) {
                reportError(lineNumber, origLine, "find valid variable name");
                errorOccurred = true;
                break;
            }
            size_t startIdent = pos;
            while(pos < rest.size() && (isalnum(rest[pos]) || rest[pos]=='_'))
                pos++;
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
                double val = evaluateExpression(rest, variables);
                variables[varName] = val;
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
            string value = valuePart.substr(1, valuePart.size() - 2);
            stringVariables[varName] = value;
        }
        else if(lineTrimmed.rfind("Bool", 0) == 0) {
            string rest = trim(lineTrimmed.substr(4));
            size_t eqPos = rest.find('=');
            if(eqPos == string::npos) {
                reportError(lineNumber, origLine, "find '=' in Bool declaration");
                errorOccurred = true;
                break;
            }
            string varPart = trim(rest.substr(0, eqPos));
            string exprPart = trim(rest.substr(eqPos + 1));
            if (exprPart.back() == ';') {
                exprPart = exprPart.substr(0, exprPart.size() - 1);
            }
            exprPart = trim(exprPart);
            double value;
            if (exprPart == "true") {
                value = 1.0;
            } else if (exprPart == "false") {
                value = 0.0;
            } else {
                reportError(lineNumber, origLine, "invalid boolean value");
                errorOccurred = true;
                break;
            }
            size_t endVar = varPart.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789");
            string varName = varPart.substr(0, endVar);
            variables[varName] = value;
        }
        else if(lineTrimmed.rfind("print", 0) == 0) {
            size_t pos = 5;
            while(pos < lineTrimmed.size() && isspace(lineTrimmed[pos]))
                pos++;
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
            while(pos < lineTrimmed.size() && isspace(lineTrimmed[pos]))
                pos++;
            if(pos >= lineTrimmed.size() || lineTrimmed[pos] != ';') {
                reportError(lineNumber, origLine, "find ';' at end of print call");
                errorOccurred = true;
                break;
            }
            pos++;
            while(pos < lineTrimmed.size() && isspace(lineTrimmed[pos]))
                pos++;
            if(pos != lineTrimmed.size()) {
                reportError(lineNumber, origLine, "extra tokens after ';'");
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
            if(outputArg.size() >= 2 && outputArg.front()=='\"' && outputArg.back()=='\"') {
                outStr = outputArg.substr(1, outputArg.size()-2);
            } else {
                auto it = stringVariables.find(outputArg);
                if (it != stringVariables.end()) {
                    outStr = it->second;
                } else {
                    try {
                        double result = evaluateExpression(outputArg, variables);
                        outStr = formatNumber(result);
                    } catch(exception &e) {
                        reportError(lineNumber, origLine, e.what());
                        errorOccurred = true;
                        break;
                    }
                }
            }
            outputs.push_back(outStr);
            outLengths.push_back(outStr.size() + 2);
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
                double conditionValue = evaluateExpression(conditionExpr, variables);
                
                if (conditionValue != 0.0) {
                    size_t blockStart = lineTrimmed.find('{', closeParen + 1);
                    int braceLevel = 1;
                    vector<string> blockContent;
                    size_t originalLineIndex = lineIndex;

                    if (blockStart == string::npos) {
                        lineIndex++;
                        if (lineIndex >= lines.size()) {
                            reportError(lineNumber, origLine, "missing '{' after if");
                            errorOccurred = true;
                            break;
                        }
                        blockStart = lines[lineIndex].find('{');
                    }

                    while (lineIndex < lines.size()) {
                        string currentLine = lines[lineIndex];
                        bool foundClosing = false;
                        
                        for (size_t i = (lineIndex == originalLineIndex) ? blockStart : 0; 
                             i < currentLine.size(); 
                             i++) 
                        {
                            if (currentLine[i] == '{') braceLevel++;
                            if (currentLine[i] == '}') braceLevel--;
                            
                            if (braceLevel == 0) {
                                foundClosing = true;
                                break;
                            }
                        }

                        blockContent.push_back(currentLine);
                        lineIndex++;
                        lineNumber++;
                        
                        if (foundClosing) {
                            lineIndex--;
                            lineNumber--;
                            break;
                        }
                    }

                    if (braceLevel != 0) {
                        reportError(lineNumber, origLine, "unclosed block in if");
                        errorOccurred = true;
                        break;
                    }

                    for (auto& bline : blockContent) {
                        string trimmed = trim(bline);
                        
                        if (bline == blockContent.front()) {
                            size_t firstBrace = bline.find('{');
                            if (firstBrace != string::npos) {
                                bline = bline.substr(firstBrace + 1);
                            }
                        }
                        if (bline == blockContent.back()) {
                            size_t lastBrace = bline.rfind('}');
                            if (lastBrace != string::npos) {
                                bline = bline.substr(0, lastBrace);
                            }
                        }

                        if (!trim(bline).empty()) {
                            lines.insert(lines.begin() + lineIndex, bline);
                        }
                    }
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
    
    if(errorOccurred)
        return 1;
    
    fs::path tmpDir = "tmp";
    try {
        fs::create_directory(tmpDir);
    } catch(exception &e) {
        cerr << "Error creating tmp directory: " << e.what() << endl;
        return 1;
    }
    
    ostringstream llvmIR;
    llvmIR << "; ModuleID = \"trust_module\"\n";
    llvmIR << "declare i32 @printf(i8*, ...)\n\n";
    
    vector<string> globalNames;
    for(size_t i = 0; i < outputs.size(); i++) {
        string globalName = ".str." + to_string(i);
        globalNames.push_back(globalName);
        llvmIR << llvmGlobalString(outputs[i], globalName, outLengths[i]) << "\n";
    }
    llvmIR << "\n";
    
    llvmIR << "define i32 @main() {\nentry:\n";
    for(size_t i = 0; i < globalNames.size(); i++) {
        llvmIR << "  call i32 (i8*, ...) @printf(i8* getelementptr inbounds (["
               << outLengths[i] << " x i8], ["
               << outLengths[i] << " x i8]* @" << globalNames[i] << ", i32 0, i32 0))\n";
    }
    llvmIR << "  ret i32 0\n";
    llvmIR << "}\n";
    
    fs::path llvmFile = tmpDir / "output.ll";
    ofstream outLL(llvmFile);
    if(!outLL) {
        cerr << "Error: Cannot open file " << llvmFile << " for writing." << endl;
        return 1;
    }
    outLL << llvmIR.str();
    outLL.close();
    
    fs::path inputPath = inputFileName;
    fs::path binPath = inputPath.parent_path() / inputPath.stem();
    
    string compileCmd = "clang " + llvmFile.string() + " -o " + binPath.string();
    int ret = system(compileCmd.c_str());
    if(ret != 0) {
        cerr << "Compilation with clang failed." << endl;
        return 1;
    }
    
    try {
        fs::remove_all(tmpDir);
    } catch(exception &e) {
        cerr << "Error removing tmp directory: " << e.what() << endl;
    }
    
    return 0;
}