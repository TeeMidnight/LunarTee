import os, json
import re

Translate = {}
JsonTranslate = {}
SortTranslate = {}

def ReadFile(path):
    File = open(path, "r")
    for line in File.readlines():
        line = line.strip()
        
        string = re.findall("_\s*\(\s*\"([^\"]+)*\"\s*\)", line)
        if len(string) > 0:
            TranslateString : str = string[0]
            Translate.setdefault(TranslateString, "")

    File.close()

def ReadJson():

    jsonFileName = "data/json/bot.json"

    jsonFile = open(jsonFileName, "r")
    data = json.loads(jsonFile.read())
    
    for i in data:
        JsonTranslate.setdefault(str(i['name']), "")

    for i in data:
        for j in i['drops']:
            JsonTranslate.setdefault(str(j['name']), "")

    jsonFileName = "data/json/weapons.json"
    jsonFile = open(jsonFileName, "r")
    data = json.loads(jsonFile.read())

    for i in data:
        JsonTranslate.setdefault(str(i['item']), "")
        if 'item_ammo' in i:
            JsonTranslate.setdefault(str(i['item_ammo']), "")

    def ReadJsonFile(path):
        jsonFile = open(path, "r")
        data = json.loads(jsonFile.read())
            
        if "need" in data:
            for j in data['need']:
                JsonTranslate.setdefault(j['name'], "")
        if "give" in data:
            for j in data['give']:
                JsonTranslate.setdefault(j['name'], "")

    for i in os.listdir("data/json/items/"):
        parentdir = os.path.join("data/json/items/", i)
        if os.path.isdir(parentdir):
            JsonTranslate.setdefault(i, "")
            for j in os.listdir(parentdir):
                childdir = os.path.join(parentdir, j)
                if os.path.isfile(childdir):
                    ReadJsonFile(childdir)

    for key in sorted(JsonTranslate):
        SortTranslate.setdefault(key, "")

    jsonFile.close()


def ReadDir(dir):
    for i in os.listdir(dir):
        fulldir = os.path.join(dir, i)
        if os.path.isdir(fulldir):
            ReadDir(fulldir)
        elif os.path.isfile(fulldir):
            ReadFile(fulldir)

def ReadLanguageFile(path):
    for key in SortTranslate:
        SortTranslate[key] = ""

    langfile = open(path, "r")

    nextkey = ""
    for line in langfile.readlines():
        line = line.strip()

        if line[:1] == "=":
            nextkey = line[2:]
        if line[:2] == "##" and nextkey in SortTranslate:
            SortTranslate[nextkey] = line[3:]

    langfile.close()
    

def WriteLanguageFile(path):
    langfile = open(path, "w")

    print(f"LunarTee translation", end="\n\n", file=langfile)

    for key in SortTranslate:
        print(f"= {key}", end="\n", file=langfile)
        print(f"## {SortTranslate[key]}", end="\n", file=langfile)
        print("", end="\n", file=langfile)

    langfile.close()

if __name__ == '__main__':
    ReadDir("src/")
    
    for key in sorted(Translate):
        SortTranslate.setdefault(key, "")

    ReadJson()


    languagefiles = ['zh-CN']

    for filename in languagefiles:
        if os.path.exists(f"data/server_lang/{filename}.lang") == False:
            WriteLanguageFile(f"data/server_lang/{filename}.lang")
        else:
            ReadLanguageFile(f"data/server_lang/{filename}.lang")
            WriteLanguageFile(f"data/server_lang/{filename}.lang")