# Эмулятор ЭВМ М-20 #
Исходные тексты можно скачать командой:
```
svn checkout http://m20.googlecode.com/svn/trunk/ m20-read-only
```

### Состояние на 29-03-2008 ###

  * Создан ассемблер с мнемоникой, близкой к БЭСМ-6. Имеется также простой дизассемблер.
  * В эмуляторе реализованы все машинные инструкции, включая обращения к внешним устройствам.
  * Работают [пример 1](http://code.google.com/p/m20/source/browse/trunk/as/example1.m20) и [пример 2](http://code.google.com/p/m20/source/browse/trunk/as/example2.m20), вычисляя и печатая правильные значения.
  * Работает печать на АЦПУ: десятичные, восьмеричные числа, а также текстовая информация. В файле [as/example3.m20](http://code.google.com/p/m20/source/browse/trunk/as/example3.m20) имеется пример печати.
  * Выполняется обмен с магнитным барабаном. Образ барабана (16 килослов) хранится в файле $HOME/.m20/drum.bin.
  * Работает интерпретирующая система, пример вызова стандартной программы sin(x) можно посмотреть в файле [as/example4.m20](http://code.google.com/p/m20/source/browse/trunk/as/example4.m20) . Коды ИС-2 размещены в файле [as/is2.m20](http://code.google.com/p/m20/source/browse/trunk/as/is2.m20).
  * Начата реализация библиотеки стандартных программ (в файле [as/stdprog.m20](http://code.google.com/p/m20/source/browse/trunk/as/stdprog.m20)). Пока имеется только СП-05 - вычисление sin(x). Пример вызова СП-05 расположен в файле [as/example4.m20](http://code.google.com/p/m20/source/browse/trunk/as/example4.m20).

### Планы развития ###
  * Документировать формат входного файла эмулятора.
  * Документировать язык ассемблера.
  * Добавить поддержку кодировок koi8-r, cp1251 и cp866. Сейчас эмулятор будет правильно работать только на системах с локальной кодировкой Unicode utf-8.
  * Разыскать коды трансляторов [Алгол-60 для М-20](http://www.osp.ru/cw/1999/45/38679/):
    * [ТА-1м](http://www.computer-museum.ru/histsoft/ta-1m.htm) имени [Лаврова](http://www.osp.ru/cw/2004/29/78845/);
    * [ТА-2м](http://www.computer-museum.ru/histsoft/ta-2m.htm) имени [Шуры-Буры](http://www.rustrana.ru/print.php?nid=5602);
    * Альфа имени [Ершова](http://www.computer-museum.ru/galglory/ershov1.htm).
  * Реализовать компилятор Си, взяв за основу [cc17](http://vak.ru/doku.php/proj/pic/pic17-c-compiler).