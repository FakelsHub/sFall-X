/*
 *    sfall
 *    Copyright (C) 2008-2021  The sfall team
 *
 */

#include "FalloutEngine\VariableOffsets.h"
#include "IniReader.h"

#include "Translate.h"

namespace sfall
{

static struct Translation {
	char def[65];
	char lang[256];
	bool state = false;

	const char* File() {
		return (state) ? lang : def;
	}
} translationIni;

size_t Translate::Get(const char* section, const char* setting, const char* defaultValue, char* buffer, size_t bufSize) {
	return IniReader::GetString(section, setting, defaultValue, buffer, bufSize, translationIni.File());
}

std::string Translate::Get(const char* section, const char* setting, const char* defaultValue, size_t bufSize) {
	return std::move(IniReader::GetString(section, setting, defaultValue, bufSize, translationIni.File()));
}

std::vector<std::string> Translate::GetList(const char* section, const char* setting, const char* defaultValue, char delimiter, size_t bufSize) {
	return std::move(IniReader::GetList(section, setting, defaultValue, bufSize, delimiter, translationIni.File()));
}

/////////////////////////////////////////////////////////////////////////////////////////

static void MakeLangTranslationPath(const char* config) {
	char patches[65], language[65];
	char fileConfig[65] = ".\\";

	if (config[0] == '\0') config = (const char*)FO_VAR_fallout_config;
	std::strcpy(&fileConfig[2], config);

	IniReader::GetString("system", "language", "english", language, 64, fileConfig);
	IniReader::GetString("system", "master_patches", "data", patches, 64, fileConfig);

	const char* iniDef = translationIni.def;
	while (*iniDef == '\\' || *iniDef == '/' || *iniDef == '.') iniDef++; // skip first characters
	sprintf(translationIni.lang, "%s\\text\\%s\\%s", patches, language, iniDef);

	translationIni.state = (GetFileAttributes(translationIni.lang) != INVALID_FILE_ATTRIBUTES);
}

static std::string saveSfallDataFailMsg;
static std::string combatSaveFailureMsg;
static std::string combatBlockedMessage;

std::string& Translate::SfallSaveDataFailure()   { return saveSfallDataFailMsg; }
std::string& Translate::CombatSaveBlockMessage() { return combatSaveFailureMsg; }
std::string& Translate::CombatBlockMessage()     { return combatBlockedMessage; }

static void InitMessagesTranslate() {
	combatBlockedMessage = Translate::Get("sfall", "BlockedCombat", "You cannot enter combat at this time.");
	combatSaveFailureMsg = Translate::Get("sfall", "SaveInCombat", "Cannot save at this time.");
	saveSfallDataFailMsg = Translate::Get("sfall", "SaveSfallDataFail", "ERROR saving extended savegame information! "
	                                      "Check if other programs interfere with savegame files/folders and try again!");
}

void Translate::init(const char* config) {
	IniReader::GetConfigString("Main", "TranslationsINI", ".\\Translations.ini", translationIni.def, 65);

	MakeLangTranslationPath(config);
	InitMessagesTranslate();
}

}