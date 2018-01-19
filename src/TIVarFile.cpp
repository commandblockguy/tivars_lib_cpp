/*
 * Part of tivars_lib_cpp
 * (C) 2015-2017 Adrien 'Adriweb' Bertrand
 * https://github.com/adriweb/tivars_lib_cpp
 * License: MIT
 */

#include "TIVarFile.h"
#include "utils.h"
#include "TIModels.h"
#include <numeric>
#include <regex>

using namespace std;

namespace tivars
{

    /*** Constructors ***/

    /**
     * Internal constructor, called from loadFromFile and createNew.
     * If filePath empty or not given, it's a programmer error, and it will throw in BinaryFile(filePath) anyway.
     * To create a TIVarFile not from a file, use TIVarFile::createNew
     * @param   string  filePath
     * @throws  \Exception
     */
    TIVarFile::TIVarFile(const string& filePath) : BinaryFile(filePath)
    {
        this->isFromFile = true;
        if (this->fileSize < 76) // bare minimum for header + a var entry
        {
            throw runtime_error("This file is not a valid TI-[e]z80 variable file");
        }
        this->makeHeaderFromFile();
        this->makeVarEntryFromFile();
        this->computedChecksum = this->computeChecksumFromFileData();
        this->inFileChecksum = this->getChecksumValueFromFile();
        if (this->computedChecksum != this->inFileChecksum)
        {
            // puts("[Warning] File is corrupt (read and calculated checksums differ)");
            this->corrupt = true;
        }
        this->type = TIVarType::createFromID(this->varEntry.typeID);
        this->close(); // let's free the resource up as early as possible
    }

    TIVarFile TIVarFile::loadFromFile(const string& filePath)
    {
        if (!filePath.empty())
        {
            return TIVarFile(filePath);
        } else {
            throw runtime_error("No file path given");
        }
    }

    TIVarFile::TIVarFile(const TIVarType& type, const string& name, const TIModel& model) : type(type), calcModel(model)
    {
        if (!this->calcModel.supportsType(this->type))
        {
            throw runtime_error("This calculator model (" + this->calcModel.getName() + ") does not support the type " + this->type.getName());
        }

        string newName = this->fixVarName(name);
        string signature = this->calcModel.getSig(); std::copy(signature.begin(), signature.end(), this->header.signature);
        uchar sig_extra[3] = {0x1A, 0x0A, 0x00};       std::copy(sig_extra, sig_extra + 3, this->header.sig_extra);
        string comment = str_pad("Created by tivars_lib_cpp", 42, "\0"); std::copy(comment.begin(), comment.end(), this->header.comment);
        this->header.entries_len = 0; // will have to be overwritten later

        this->varEntry.meta_length  = (this->calcModel.getFlags() >= TIFeatureFlags::hasFlash) ? varEntryNewLength : varEntryOldLength;
        this->varEntry.data_length  = 0; // will have to be overwritten later
        this->varEntry.typeID       = (uchar) type.getId();
        string varname = str_pad(newName, 8, "\0");    std::copy(varname.begin(), varname.begin() + 7, this->varEntry.varname);
        this->varEntry.data_length2 = 0; // will have to be overwritten later
    }

    TIVarFile TIVarFile::createNew(const TIVarType& type, const string& name, const TIModel& model)
    {
        return TIVarFile(type, name, model);
    }

    TIVarFile TIVarFile::createNew(const TIVarType& type, const string& name)
    {
        return createNew(type, name, TIModel::createFromName("84+"));
    }

    TIVarFile TIVarFile::createNew(const TIVarType& type)
    {
        return createNew(type, "");
    }

    /*** Makers ***/

    void TIVarFile::makeHeaderFromFile()
    {
        rewind(this->file);

        string signature = this->get_string_bytes(8);  std::copy(signature.begin(), signature.end(), this->header.signature);
        auto sig_extra   = this->get_raw_bytes(3);     std::copy(sig_extra.begin(), sig_extra.end(), this->header.sig_extra);
        string comment   = this->get_string_bytes(42); std::copy(comment.begin(),   comment.end(),   this->header.comment);
        this->header.entries_len  = (uint16_t)((this->get_raw_bytes(1)[0] & 0xFF) + (this->get_raw_bytes(1)[0] << 8));
        this->calcModel = TIModel::createFromSignature(signature);
    }

    void TIVarFile::makeVarEntryFromFile()
    {
        fseek(this->file, TIVarFile::dataSectionOffset, SEEK_SET);

        this->varEntry.meta_length  = (uint16_t)((this->get_raw_bytes(1)[0] & 0xFF) + (this->get_raw_bytes(1)[0] << 8));
        this->varEntry.data_length  = (uint16_t)((this->get_raw_bytes(1)[0] & 0xFF) + (this->get_raw_bytes(1)[0] << 8));
        this->varEntry.typeID       = this->get_raw_bytes(1)[0];
        string varname              = this->get_string_bytes(8); std::copy(varname.begin(), varname.begin() + 7, this->varEntry.varname);
        if (this->calcModel.getFlags() >= TIFeatureFlags::hasFlash)
        {
            this->varEntry.version      = this->get_raw_bytes(1)[0];
            this->varEntry.archivedFlag = this->get_raw_bytes(1)[0];
        }
        this->varEntry.data_length2  = (uint16_t)((this->get_raw_bytes(1)[0] & 0xFF) + (this->get_raw_bytes(1)[0] << 8));
        this->varEntry.data          = this->get_raw_bytes(this->varEntry.data_length);
    }


    /*** Private actions ***/

    uint16_t TIVarFile::computeChecksumFromFileData()
    {
        if (this->isFromFile)
        {
            fseek(this->file, TIVarFile::dataSectionOffset, SEEK_SET);

            uint16_t sum = 0;
            for (size_t i = dataSectionOffset; i < this->fileSize - 2; i++)
            {
                sum += this->get_raw_bytes(1)[0];
            }
            return (uint16_t) (sum & 0xFFFF);
        } else {
            throw runtime_error("[Error] No file loaded");
        }
    }

    uint16_t TIVarFile::computeChecksumFromInstanceData()
    {
        uint16_t sum = 0;
        sum += this->varEntry.meta_length;
        sum += this->varEntry.typeID;
        sum += 2 * ((this->varEntry.data_length & 0xFF) + ((this->varEntry.data_length >> 8) & 0xFF));
        sum += std::accumulate(this->varEntry.varname, this->varEntry.varname + 8, 0);
        sum += std::accumulate(this->varEntry.data.begin(), this->varEntry.data.end(), 0);
        if (this->calcModel.getFlags() >= TIFeatureFlags::hasFlash)
        {
            sum += this->varEntry.version;
            sum += this->varEntry.archivedFlag;
        }
        return (uint16_t) (sum & 0xFFFF);
    }

    uint16_t TIVarFile::getChecksumValueFromFile()
    {
        if (this->isFromFile)
        {
            fseek(this->file, this->fileSize - 2, SEEK_SET);
            return this->get_raw_bytes(1)[0] + (this->get_raw_bytes(1)[0] << 8);
        } else {
            throw runtime_error("[Error] No file loaded");
        }
    }

    /**
     *  Updates the length fields in both the header and the var entry, as well as the checksum
     */
    void TIVarFile::refreshMetadataFields()
    {
        // todo : recompute correctly for multiple var entries
        this->varEntry.data_length2 = this->varEntry.data_length = (uint16_t) this->varEntry.data.size();

        // The + 2 + 2 is because of the length of both length field themselves
        this->header.entries_len = (uint16_t)2 + (uint16_t)2 + this->varEntry.data_length
                                 + ((this->calcModel.getFlags() >= TIFeatureFlags::hasFlash) ? varEntryNewLength : varEntryOldLength);

        this->computedChecksum = this->computeChecksumFromInstanceData();
    }

    string TIVarFile::fixVarName(const string& name)
    {
        string newName(name);
        if (newName.empty())
        {
            newName = "FILE" + (type.getExts().empty() ? "" : type.getExts()[0]);
        }
        newName = regex_replace(newName, regex("[^a-zA-Z0-9]"), "");
        if (newName.length() > 8 || newName.empty() || is_numeric(newName.substr(0, 1)))
        {
            throw invalid_argument("Invalid name given. 8 chars (A-Z, 0-9) max, starting by a letter");
        }

        for (auto & c: newName) c = (char) toupper(c);

        newName = str_pad(newName, 8, "\0");

        return newName;
    }

    /*** Public actions **/

    /**
    * @param    array   data   The array of bytes
    */
    void TIVarFile::setContentFromData(const data_t& data)
    {
        if (!data.empty())
        {
            this->varEntry.data = data;
            this->refreshMetadataFields();
        } else {
            throw runtime_error("[Error] No data given");
        }
    }

    void TIVarFile::setContentFromString(const string& str, const options_t& options)
    {
        this->varEntry.data = (this->type.getHandlers().first)(str, options);
        this->refreshMetadataFields();
    }
    void TIVarFile::setContentFromString(const string& str)
    {
        setContentFromString(str, {});
    }

    void TIVarFile::setCalcModel(const TIModel& model)
    {
        this->calcModel = model;
        string signature = model.getSig();
        std::copy(signature.begin(), signature.end(), this->header.signature);
    }

    void TIVarFile::setVarName(const string& name)
    {
        string varname = TIVarFile::fixVarName(name);
        std::copy(varname.begin(), varname.begin() + 7, this->varEntry.varname);
        this->refreshMetadataFields();
    }

    void TIVarFile::setArchived(bool flag)
    {
        if (this->calcModel.getFlags() >= TIFeatureFlags::hasFlash)
        {
            this->varEntry.archivedFlag = (uchar)(flag ? 1 : 0);
            this->refreshMetadataFields();
        } else {
            throw runtime_error("[Error] Archived flag not supported on this calculator model");
        }
    }


    data_t TIVarFile::getRawContent()
    {
        return this->varEntry.data;
    }

    string TIVarFile::getReadableContent(const options_t& options)
    {
        return (this->type.getHandlers().second)(this->varEntry.data, options);
    }
    string TIVarFile::getReadableContent()
    {
        return getReadableContent({});
    }

    data_t TIVarFile::make_bin_data()
    {
        data_t bin_data;

        // Header
        {
            bin_data.insert(bin_data.end(), this->header.signature, this->header.signature + 8);
            bin_data.insert(bin_data.end(), this->header.sig_extra, this->header.sig_extra + 3);
            bin_data.insert(bin_data.end(), this->header.comment,   this->header.comment   + 42);
            bin_data.push_back((uchar) (this->header.entries_len & 0xFF)); bin_data.push_back((uchar) ((this->header.entries_len >> 8) & 0xFF));
        }

        // Var entry
        {
            bin_data.push_back((uchar) (this->varEntry.meta_length & 0xFF)); bin_data.push_back((uchar) ((this->varEntry.meta_length >> 8) & 0xFF));
            bin_data.push_back((uchar) (this->varEntry.data_length & 0xFF)); bin_data.push_back((uchar) ((this->varEntry.data_length >> 8) & 0xFF));
            bin_data.push_back(this->varEntry.typeID);
            bin_data.insert(bin_data.end(), this->varEntry.varname, this->varEntry.varname + 8);
            if (this->calcModel.getFlags() >= TIFeatureFlags::hasFlash)
            {
                bin_data.push_back(this->varEntry.version);
                bin_data.push_back(this->varEntry.archivedFlag);
            }
            bin_data.push_back((uchar) (this->varEntry.data_length2 & 0xFF)); bin_data.push_back((uchar) ((this->varEntry.data_length2 >> 8) & 0xFF));
            bin_data.insert(bin_data.end(), this->varEntry.data.begin(), this->varEntry.data.end());
        }

        return bin_data;
    }

    /**
     * Writes a variable to an actual file on the FS
     * If the variable was already loaded from a file, it will be used and overwritten,
     * except if a specific directory and name are provided.
     *
     * @param   string  directory  Directory to save the file to
     * @param   string  name       Name of the file, without the extension
     */
    string TIVarFile::saveVarToFile(string directory, string name)
    {
        string fullPath;
        FILE* handle;

        if (this->isFromFile && directory.empty())
        {
            fullPath = this->filePath;
        } else {
            if (name.empty())
            {
                string tmp;
                for (unsigned char c : this->varEntry.varname)
                {
                    if (c)
                    {
                        tmp += c;
                    } else {
                        break;
                    }
                }
                name = tmp;
            }
            int extIndex = this->calcModel.getOrderId();
            if (extIndex < 0)
            {
                extIndex = 0;
            }
            string fileName = name + "." + this->getType().getExts()[extIndex];
            if (directory.empty())
            {
                directory = ".";
            }
            fullPath = directory + "/" + fileName;
        }

        handle = fopen(fullPath.c_str(), "wb");
        if (!handle)
        {
            throw runtime_error("Can't open the input file");
        }

        this->refreshMetadataFields();

        // Make and write file data
        data_t bin_data = make_bin_data();
        fwrite(&bin_data[0], bin_data.size(), sizeof(uchar), handle);

        // Write checksum
        const char buf[2] = {(char) (this->computedChecksum & 0xFF), (char) ((this->computedChecksum >> 8) & 0xFF)};
        fwrite(buf, sizeof(char), 2, handle);

        fclose(handle);

        this->corrupt = false;

        return fullPath;
    }

    string TIVarFile::saveVarToFile()
    {
        return saveVarToFile("", "");
    }


#ifdef __EMSCRIPTEN__
    #include <emscripten/bind.h>
    using namespace emscripten;
    EMSCRIPTEN_BINDINGS(_tivarfile) {

            register_map<std::string, int>("options_t");

            class_<TIVarFile>("TIVarFile")
                    .function("getHeader"                , &TIVarFile::getHeader)
                    .function("getVarEntry"              , &TIVarFile::getVarEntry)
                    .function("getType"                  , &TIVarFile::getType)
                    .function("getInstanceChecksum"      , &TIVarFile::getInstanceChecksum)

                    .function("getChecksumValueFromFile" , &TIVarFile::getChecksumValueFromFile)
                    .function("setContentFromData"       , &TIVarFile::setContentFromData)
                    .function("setContentFromString"     , select_overload<void(const std::string&, const options_t&)>(&TIVarFile::setContentFromString))
                    .function("setContentFromString"     , select_overload<void(const std::string&)>(&TIVarFile::setContentFromString))
                    .function("setCalcModel"             , &TIVarFile::setCalcModel)
                    .function("setVarName"               , &TIVarFile::setVarName)
                    .function("setArchived"              , &TIVarFile::setArchived)
                    .function("getRawContent"            , &TIVarFile::getRawContent)
                    .function("getReadableContent"       , select_overload<std::string(const options_t&)>(&TIVarFile::getReadableContent))
                    .function("getReadableContent"       , select_overload<std::string(void)>(&TIVarFile::getReadableContent))

                    .function("saveVarToFile"            , select_overload<std::string(std::string, std::string)>(&TIVarFile::saveVarToFile))
                    .function("saveVarToFile"            , select_overload<std::string(void)>(&TIVarFile::saveVarToFile))

                    .class_function("loadFromFile", &TIVarFile::loadFromFile)
                    .class_function("createNew", select_overload<TIVarFile(const TIVarType&, const std::string&, const TIModel&)>(&TIVarFile::createNew))
                    .class_function("createNew", select_overload<TIVarFile(const TIVarType&, const std::string&)>(&TIVarFile::createNew))
                    .class_function("createNew", select_overload<TIVarFile(const TIVarType&)>(&TIVarFile::createNew))
            ;
    }
#endif

}