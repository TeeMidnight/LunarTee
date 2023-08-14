import json, os

if __name__ == '__main__':
    jsonFileName = "data/json/bot.json"

    jsonFile = open(jsonFileName, "r")
    data = json.loads(jsonFile.read())

    f = open("src/generated/localize-temp.h", "w")

    print('#ifndef LOCALIZE_TEMP_H', file=f, end="\n")
    print('#define LOCALIZE_TEMP_H', file=f, end="\n\n")

    print('#define _C(CONTEXT, TEXT) TEXT', file=f, end="\n")
    
    for i in data:
        print('_C("'  + "Bot name" + '", "' + str(i['name']) + '");', file=f, end="\n")

    for i in data:
        for j in i['drops']:
            print('_C("'  + "An item" + '", "' + str(j['name']) + '");', file=f, end="\n")

    jsonFileName = "data/json/weapons.json"
    jsonFile = open(jsonFileName, "r")
    data = json.loads(jsonFile.read())

    for i in data:
        print('_C("'  + "An item" + '", "' + str(i['item']) + '");', file=f, end="\n")
        if 'item_ammo' in i:
            print('_C("'  + "An item" + '", "' + str(i['item_ammo']) + '");', file=f, end="\n")

    def ReadFile(path):
        jsonFile = open(path, "r")
        data = json.loads(jsonFile.read())

        print('_C("'  + "An item" + '", "' + data['name'] + '");', file=f, end="\n")
            
        if "need" in data:
            for j in data['need']:
                print('_C("'  + "An item" + '", "' + j['name'] + '");', file=f, end="\n")
        if "give" in data:
            for j in data['give']:
                print('_C("'  + "An item" + '", "' + j['name'] + '");', file=f, end="\n")

    for i in os.listdir("data/json/items/"):
        parentdir = os.path.join("data/json/items/", i)
        if os.path.isdir(parentdir):
            print('_C("'  + "An item type" + '", "' + i + '");', file=f, end="\n")
            for j in os.listdir(parentdir):
                childdir = os.path.join(parentdir, j)
                if os.path.isfile(childdir):
                    ReadFile(childdir)

    print('', file=f, end="\n")
    print('#endif', file=f, end="\n")


    jsonFile.close()
    f.close()

