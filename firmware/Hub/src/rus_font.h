// rus_font.h - Таблица соответствия русских букв
#ifndef RUS_FONT_H
#define RUS_FONT_H

// Простая таблица транслитерации (замена русских букв на английские)
const char* rusToEng(const char* rus) {
    static char buffer[50];
    int j = 0;
    for (int i = 0; rus[i] != '\0' && j < 49; i++) {
        switch(rus[i]) {
            case 'А': case 'а': buffer[j++] = 'A'; break;
            case 'Б': case 'б': buffer[j++] = 'B'; break;
            case 'В': case 'в': buffer[j++] = 'V'; break;
            case 'Г': case 'г': buffer[j++] = 'G'; break;
            case 'Д': case 'д': buffer[j++] = 'D'; break;
            case 'Е': case 'е': buffer[j++] = 'E'; break;
            case 'Ё': case 'ё': buffer[j++] = 'E'; break;
            case 'Ж': case 'ж': buffer[j++] = 'Z'; buffer[j++] = 'h'; break;
            case 'З': case 'з': buffer[j++] = 'Z'; break;
            case 'И': case 'и': buffer[j++] = 'I'; break;
            case 'Й': case 'й': buffer[j++] = 'Y'; break;
            case 'К': case 'к': buffer[j++] = 'K'; break;
            case 'Л': case 'л': buffer[j++] = 'L'; break;
            case 'М': case 'м': buffer[j++] = 'M'; break;
            case 'Н': case 'н': buffer[j++] = 'N'; break;
            case 'О': case 'о': buffer[j++] = 'O'; break;
            case 'П': case 'п': buffer[j++] = 'P'; break;
            case 'Р': case 'р': buffer[j++] = 'R'; break;
            case 'С': case 'с': buffer[j++] = 'S'; break;
            case 'Т': case 'т': buffer[j++] = 'T'; break;
            case 'У': case 'у': buffer[j++] = 'U'; break;
            case 'Ф': case 'ф': buffer[j++] = 'F'; break;
            case 'Х': case 'х': buffer[j++] = 'H'; break;
            case 'Ц': case 'ц': buffer[j++] = 'C'; break;
            case 'Ч': case 'ч': buffer[j++] = 'C'; buffer[j++] = 'h'; break;
            case 'Ш': case 'ш': buffer[j++] = 'S'; buffer[j++] = 'h'; break;
            case 'Щ': case 'щ': buffer[j++] = 'S'; buffer[j++] = 'h'; break;
            case 'Ъ': case 'ъ': buffer[j++] = '\''; break;
            case 'Ы': case 'ы': buffer[j++] = 'Y'; break;
            case 'Ь': case 'ь': buffer[j++] = '\''; break;
            case 'Э': case 'э': buffer[j++] = 'E'; break;
            case 'Ю': case 'ю': buffer[j++] = 'Y'; buffer[j++] = 'u'; break;
            case 'Я': case 'я': buffer[j++] = 'Y'; buffer[j++] = 'a'; break;
            default: buffer[j++] = rus[i]; break;
        }
    }
    buffer[j] = '\0';
    return buffer;
}

#endif