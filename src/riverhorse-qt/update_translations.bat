set LUPDATE=..\..\dep\msvc\qt\6.1.0\msvc2019_64\bin\lupdate.exe ./ ../core/ ../frontend-common/ -tr-function-alias translate+=TranslateString -tr-function-alias translate+=TranslateStdString -tr-function-alias QT_TRANSLATE_NOOP+=TRANSLATABLE

%LUPDATE% -ts translations\riverhorse-qt_de.ts
%LUPDATE% -ts translations\riverhorse-qt_es.ts
%LUPDATE% -ts translations\riverhorse-qt_fr.ts
%LUPDATE% -ts translations\riverhorse-qt_he.ts
%LUPDATE% -ts translations\riverhorse-qt_it.ts
%LUPDATE% -ts translations\riverhorse-qt_ja.ts
%LUPDATE% -ts translations\riverhorse-qt_nl.ts
%LUPDATE% -ts translations\riverhorse-qt_pl.ts
%LUPDATE% -ts translations\riverhorse-qt_pt-br.ts
%LUPDATE% -ts translations\riverhorse-qt_pt-pt.ts
%LUPDATE% -ts translations\riverhorse-qt_ru.ts
%LUPDATE% -ts translations\riverhorse-qt_tr.ts
%LUPDATE% -ts translations\riverhorse-qt_zh-cn.ts
pause
