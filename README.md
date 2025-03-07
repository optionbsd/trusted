# Trusted

<p align="center">
  <img src="logo.png" alt="Trusted Logo" width="400">
</p>

Trusted - простой язык программирования, использующий LLVM для компиляции. 

## Синтаксис
 - `Integer <VariableName> = <VariableValue>;` - создает переменную типа integer
 - `String <VariableName> = <VariableValue>;` - создает переменную типа string
 - `Bool <VariableName> = <true/false>;` - создает переменную типа bool с значением ture или false
 - `Array <ArrayName> = [<data>, <data>...];` - создает массив
 - `FunctionName(args...);` - вызывает функцию с аругментами
 - `"Value"` - строка
 - `<IntValue>` - целое число
 - `
   if (condition) {
      action();
   }
   ` - если `condition` эквивалентно true то выполняется `action();`

  - `!<condition>` - логическое отрицание
  - `//<text>` - комментарий. Просто будет пропущен компилятором
  - `Memory <functionName> = {<code>}` - создание функции

## Примеры кода
  ```
  print("Hello, world!");
  ```
  - просто выведет `Hello, world!`



    

  ```
  Integer myInt = 2+2*2;
  print(myInt);
  ```
  - выведет `6`



    

  ```
  Bool myBool = true;
  Bool mySecondBool = false;
  
  if (myBool) {
      print("Yeah");
  }
  
  if (mySecondBool) {
      print("Nah");
  }
  ```
  - выведет `Yeah` так как `myBool` - true, а `mySecondBool` - false

## Сборка Trusted кода

Просто используйте `trustc <путь к коду>`. На выходе вы получите исполняемый файл

## Сборка компилятора

Зависимости:

```
clang
llvm
make
```

После установки зависимостей напишите `make`. Это автоматически соберет компилятор а так же тестовый Trusted код, после чего запустит его

Если компилятор уже собран - `make run`. Это запустит только компиляцию Trusted кода