#include <cstring>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include "src/TypeHandlers/TypeHandlers.h"
#include "src/TIVarFile.h"
#include "src/TIModels.h"
#include "src/TIVarTypes.h"

using namespace std;
using namespace tivars;

int main(int argc, char** argv) {
    using tivars::TH_Tokenized::LANG_EN;
    TIModels::initTIModelsArray();
    TIVarTypes::initTIVarTypesArray();
    TH_Tokenized::initTokens();

    if(argc < 2) {
        cout << "Usage: " << argv[0] << " [tokenize|pack]" << endl;
        return 1;
    }

    if(strcmp(argv[1], "tokenize") == 0) {
        if(argc != 4) {
            cout << "Usage: " << argv[0] << " tokenize (infile) (outfile)" << endl;
            return 1;
        }

        TIVarFile newPrgm = TIVarFile::createNew(TIVarType::createFromName("Program"));

        ifstream in(argv[2], ios::in);
        if(!in) {
            cout << "Failed to open file" << endl;
            return 1;
        }

        ostringstream str;
        str << in.rdbuf();
        in.close();

        //cout << str.str();

        options_t options;
        options["detect_strings"] = 0;

        newPrgm.setContentFromString(str.str(), options);

        ofstream out(argv[3], ios::out | ios::binary);
        out.write(reinterpret_cast<const char*>(&newPrgm.getRawContent()[2]), newPrgm.getRawContent().size() - 2);

        return 0;
    } else if(strcmp(argv[1], "pack") == 0) {
        if(argc != 6) {
            cout << "Usage: " << argv[0] << " pack (infile) (outfile) (type) (name)" << endl;
            return 1;
        }

        TIVarFile var = TIVarFile::createNew(TIVarType::createFromName(argv[4]));
        var.setVarName(argv[5]);

        ifstream in(argv[2], ios::in);
        if(!in) {
            cout << "Failed to open file" << endl;
            return 1;
        }
        in.seekg(0, ios::end);
        size_t filesize = in.tellg();
        in.seekg(0, ios::beg);

        data_t data;
        data.resize(filesize + 2);
        *(uint16_t*)&data[0] = filesize;
        in.read((char *)&data[2], filesize);
        in.close();

        var.setContentFromData(data);

        var.saveVarToFile(".", argv[3]);
    }
}