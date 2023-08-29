import os, json
import re

Translate = {}

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
        Translate.setdefault(str(i['name']), "")

    for i in data:
        for j in i['drops']:
            Translate.setdefault(str(j['name']), "")

    jsonFileName = "data/json/weapons.json"
    jsonFile = open(jsonFileName, "r")
    data = json.loads(jsonFile.read())

    for i in data:
        Translate.setdefault(str(i['item']), "")
        if 'item_ammo' in i:
            Translate.setdefault(str(i['item_ammo']), "")

    def ReadJsonFile(path):
        jsonFile = open(path, "r")
        data = json.loads(jsonFile.read())
            
        if "need" in data:
            for j in data['need']:
                Translate.setdefault(j['name'], "")
        if "give" in data:
            for j in data['give']:
                Translate.setdefault(j['name'], "")

    for i in os.listdir("data/json/items/"):
        parentdir = os.path.join("data/json/items/", i)
        if os.path.isdir(parentdir):
            Translate.setdefault(i, "")
            for j in os.listdir(parentdir):
                childdir = os.path.join(parentdir, j)
                if os.path.isfile(childdir):
                    ReadJsonFile(childdir)

    jsonFile.close()


def ReadDir(dir):
    for i in os.listdir(dir):
        fulldir = os.path.join(dir, i)
        if os.path.isdir(fulldir):
            ReadDir(fulldir)
        elif os.path.isfile(fulldir):
            ReadFile(fulldir)

def ReadLanguageFile(path):
    for key in Translate:
        Translate[key] = ""

    langfile = open(path, "r")

    nextkey = ""
    for line in langfile.readlines():
        line = line.strip()

        if line[:1] == "=":
            nextkey = line[2:]
        if line[:2] == "##" and nextkey in Translate:
            Translate[nextkey] = line[3:]

    langfile.close()
    

def WriteLanguageFile(path):
    langfile = open(path, "w")

    print(f"LunarTee translation", end="\n\n", file=langfile)

    for key in Translate:
        print(f"= {key}", end="\n", file=langfile)
        print(f"## {Translate[key]}", end="\n", file=langfile)
        print("", end="\n", file=langfile)

    langfile.close()

if __name__ == '__main__':
    ReadDir("src/")
    ReadJson()

    languagefiles = ['zh-CN']

    for filename in languagefiles:
        if os.path.exists(f"data/server_lang/{filename}.lang") == False:
            WriteLanguageFile(f"data/server_lang/{filename}.lang")
        else:
            ReadLanguageFile(f"data/server_lang/{filename}.lang")
            WriteLanguageFile(f"data/server_lang/{filename}.lang")