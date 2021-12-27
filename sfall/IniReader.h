/*
 *    sfall
 *    Copyright (C) 2008-2021  The sfall team
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

namespace sfall
{

class IniReader {
public:
	static void init();

	static DWORD IniReader::modifiedIni;

	static void IniReader::SetDefaultConfigFile();
	static void IniReader::SetConfigFile(const char* iniFile);
	static const char* IniReader::GetConfigFile();

	// Gets the value from the default config (i.e. ddraw.ini)
	static int IniReader::GetIntDefaultConfig(const char* section, const char* setting, int defaultValue);

	static std::string IniReader::GetStringDefaultConfig(const char* section, const char* setting, const char* defaultValue, size_t bufSize);

	// Gets a list of values from the default config (i.e. ddraw.ini)
	static std::vector<std::string> IniReader::GetListDefaultConfig(const char* section, const char* setting, const char* defaultValue, size_t bufSize, char delimiter);

	// Gets the integer value from Sfall configuration INI file.
	static int IniReader::GetConfigInt(const char* section, const char* setting, int defaultValue);

	// Gets the string value from Sfall configuration INI file with trim function.
	static std::string IniReader::GetConfigString(const char* section, const char* setting, const char* defaultValue, size_t bufSize = 128);

	// Loads the string value from Sfall configuration INI file into the provided buffer.
	static size_t IniReader::GetConfigString(const char* section, const char* setting, const char* defaultValue, char* buffer, size_t bufSize = 128);

	// Parses the comma-separated list from the settings from Sfall configuration INI file.
	static std::vector<std::string> IniReader::GetConfigList(const char* section, const char* setting, const char* defaultValue, size_t bufSize = 128);

	// Gets the integer value from given INI file.
	static int IniReader::GetInt(const char* section, const char* setting, int defaultValue, const char* iniFile);

	// Gets the string value from given INI file.
	static std::string IniReader::GetString(const char* section, const char* setting, const char* defaultValue, size_t bufSize, const char* iniFile);

	// Gets the string value from given INI file.
	static size_t IniReader::GetString(const char* section, const char* setting, const char* defaultValue, char* buf, size_t bufSize, const char* iniFile);

	// Parses the comma-separated list setting from given INI file.
	static std::vector<std::string> GetList(const char* section, const char* setting, const char* defaultValue, size_t bufSize, char delimiter, const char* iniFile);

	static int IniReader::SetConfigInt(const char* section, const char* setting, int value);

	static int IniReader::SetDefaultConfigString(const char* section, const char* setting, const char* value);
};

// Gets the integer value from given INI file.
int iniGetInt(const char* section, const char* setting, int defaultValue, const char* iniFile);

// Gets the integer value from Sfall configuration INI file.
int GetConfigInt(const char* section, const char* setting, int defaultValue);

// Gets the string value from Sfall configuration INI file with trim function.
std::string GetConfigString(const char* section, const char* setting, const char* defaultValue, size_t bufSize = 128);

// Loads the string value from Sfall configuration INI file into the provided buffer.
size_t GetConfigString(const char* section, const char* setting, const char* defaultValue, char* buffer, size_t bufSize = 128);

}
