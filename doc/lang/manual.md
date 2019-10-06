Content
========

+ [Overview](#overview)
+ [Installing](#installing)
+ [Language Elements](#language-elements)
  + [Entry point](#entry-point)
  + [Variables](#variables)
  + [Numbers](#numbers)
  + [Mathematical Operations and Functions](#mathematical-operations-and-functions)
  + [Strings](#strings)

## Overview
A programming language is a mammoth task to develop and to learn. So encountering a new language you may ask: why another
programming language? The short answer is to try it a little differently. Why not to treat the message like a normal
language element with which different operations are possible : loading, passing, modifying, dispatching? Could we do it without using reflection all the way? This was my motivation. ELENA is designed to facilitate writing dynamic, polymorphic code, combing elements of dynamic and static languages.

ELENA is a general-purpose language with late binding. It is multi-paradigm, combining features of functional and object-oriented programming. It supports both strong and weak types, run-time conversions, boxing and unboxing primitive types, direct usage of external libraries. Rich set of tools are provided to deal with message dispatching : multi-methods, message qualifying, generic message handlers. Multiple-inheritance can be simulated using mixins and type interfaces. Built-in script engine allows to incorporate custom defined scripts into your applications. Both stand-alone applications and Virtual machine clients are supported

To summarize all of the above, let's name some functions of ELENA:
  * Free and open source (MIT licensed)
  * Complete source code
  * Unicode support (utf-8)
  * GUI IDE & Debugger
  * Optional types
  * Multiple dispatching / multi-methods
  * Support of variadic methods
  * Closures
  * Mixins
  * Type interfaces / conversions
  * Class / code templates 
  * Script Engine

## Installing

The latest version of the language can be always found at [GitHub](https://github.com/ELENA-LANG/elena-lang/releases) or [Sourceforge](https://sourceforge.net/projects/elenalang/files/). 

The simplest way to install the language is to use a setup. The zip files are provided as well. To install from them, just unzip all the files into a directory you want. You will need to add a path to BIN folder to system environment (e.g. &lt;app root&gt;\bin). The language requires Visual C++ Redistributable for VS 2017.

To open, compile or debug the programs and libraries use ELENA GUI IDE (&lt;app root&gt;\bin\elide.exe) or ELENA Command Line Compiler (&lt;app root&gt;\bin\elc.exe).

In ELENA IDE you may select **File->Open->Open Project** option and open an appropriate project file (*.prj). Then select **Project->Compile** option to compile the project, **Debug->Step Over** or **Debug->Step Into** to debug it or **Debug->Run** to execute the program.

A number of tutorials with complete source code can be found [here](https://github.com/ELENA-LANG/tutorials)

## Language Elements

We will start to learn ELENA by getting familiar with the basic program elements : programs, variables, symbol, classes, control flows.

Any ELENA program consists of source files (with **.l** extension) and the main project file (with **.prj** extension). The project file contains the main information how the source files will be compiled and what will be the result of the compilation: a library or an executable. The simplest project type is console one : it will generate a stand-alone console program.

The easiest way to generate a new project is to use IDE. Let's select **File->New->Project**

![New Project Dialog](https://user-images.githubusercontent.com/5677187/65635302-5c8e2a80-dfe0-11e9-9059-376a9ec779fb.PNG)

We have to choose the project type - console, enter the root namespace - example1 and specify the target file name - example1.

The project should contain at least one source file - **File->New->Source File**. After saving the project we are ready to write our first program.

### Entry point

A program entry is a main program code which is called at the program startup (after some preparations). When the last statement is executed the program is terminated. Any exception happens in the main code (or in nested one) will be handled by the system handler allowing graceful program exit.

To create an entry point we have to declare a public function (or to put it more exactly - a singleton closure) with a name **program** and no arguments in the project root namespace (in the simplest case in any of the project source files). Writing a function is quite straightforward for anyone familiar with C-like languages:

    public program()
    {
    }

The code is written inside curly brackets and consists of statements. A statement has to be terminated with semicolon (except the last one, where the semicolon is optional). A typical statement is a method call (a massage sending), an operation or a control-flow statement. In our case we will print a text on the system console using **console** symbol. The message argument is a string constant (enclosed in double quotes):

    public program()
    {
       console.writeLine("Hello World!");
    }

The output will be:

    Hello World! 

### Variables

A variable is a named place in the program memory (more exactly in the call stack) which contains a value. In the simplest case it is a reference to an object or null pointer - **nil** value. A variable name may contain any UTF-8 symbols (except special ones, white-spaces; the leading character cannot be a digit) and is case-sensitive. There are no reserved keywords. Variables with duplicate names (declared in the same scope) are not allowed. 

    public program()
    {
        var myFirstVariable := "Hello";
        var Δ := 1;
    }

The declaration starts with an *attribute* **var**, the bound value are placed after an assigning operator (*NOTE: 
assigning and equivalent operators are not the same in ELENA*). The value may be the result of the expression as well.

The declaration and assigning can be separated:

    public program()
    {
        var myFirstVariable;
        myFirstVariable := "Hello";
        var Δ := 1;
        Δ := Δ + 1;
    }

If a variable is declared without an assignment, its value is **nil**.

A variable type is optional in ELENA. If the type is not explicitly stated the variable is type-less (or more exactly its type is **system'Object**, the super class). It means the type is not checked and any value can be assigned.

        var v := "Hello";
        
        v := 3;

In reality there are many situations when we do care about the variable types. So let's create a strong typed variable. The required type (a class name or alias) should be written before the variable name:

        var string v := "Hello";

where **string** is an alias of **system'String** class.

**var** attribute in this case is optional and we could simplify the code:

        string v := "Hello";

The variable type restricts the possible values of the variable. The biggest difference with statically-typed languages that the type-check happens in **run-time**. So for example the following code can be compiled but will generate a run-time error:

    public program()
    {
        var o := 2;
        string v := o;
    }
  
and the output is:

    system'IntNumber : Method #cast[0] not found
    Call stack:
    system'Exception#class.new[1]:exceptions.l(96)
    system'MethodNotFoundException#class.new[1]:exceptions.l(1)
    system'MethodNotFoundException#class.new[2]:exceptions.l(190)
    system'$private'entry.#invoke[0]:win32_app.l(42)
    mytest'program.#invoke[0]:test.l(6)
    system'$private'entry.#invoke[0]:win32_app.l(37)
    system'#startUp:win32_app.l(55)

The value can be successfully converted if an appropriate conversion method is declared:

    var o := 2;
    real r := o;

In this case **system'IntNumber** class supports a conversion to **system'RealNumber** type.

The variable type can be automatically deduced from its initializer using **auto** attribute:

    auto r := 1.2r;

the variable type, **system'RealNumber** (or **real**), is defined by an assigned value - a numeric literal - **1.2r**

### Numbers

Numbers are one of the most essential data types of a programming language. ELENA supports both integers and floating-point numbers. These are:

#### Integer Types

Alias | Type | Signed? | Number of bits | Smallest value | Largest value
----- | ---- | ------- | -------------- | -------------- | --------------
byte | system'ByteNumber | No | 8 | 0 | 2^8 - 1
short | system'ShortNumber | Yes | 16 | -2^15 | 2^15-1
int | system'IntNumber | Yes | 32 | -2^31 | 2^31-1
uint | system'UIntNumber | No | 32 | 0 | 2^32-1
long | system'LongNumber | Yes | 64 | -2^63 | 2^63-1

#### Floating-point types

Alias | Type | Number of bits
----- | ---- | --------------
real | system'RealNumber | 64  

Numbers are primitive built-in types. They could be stored directly in the program memory or used as immediate values (numeric literals). Arithmetic and relational operations are natively supported. Nevertheless, thanks to just-in-time boxing they can be used as normal objects without paying much attention:

    import extensions;

    public program()
    {
       int n := 2;
       console.printLine(n);
    }

The result is    

    2

In the code above the first operation is done directly (copying immediate value into the memory) and the second one uses a boxed value (boxing a number into **system'IntNumber** object and passing it to an extension method **extensions'outputOp.printLine[]**). The appropriate types are called primitive wrappers. When it is required the wrapper value can be unboxed back into the memory.

#### Numeric literals

ELENA supports the following numeric literals:

       int  n1  := 2;             // a 32bit integer literals
       int  n2 := -23; 
       uint x  := 0Fh;            // an unsigned 32bit hexadecimal integer        
       long l1 := 1234567890123l; // a 64bit integer literals
       long l2 := -23354436l345l;
       real r1 := 23.2r;          // a floating-point literals
       real r2 := 1.2e+11r;
    
#### Numeric conversions

Numbers can be implicitly converted into each other. We may assign an integer to a long integer or a floating-point number. But the opposite is not always possible.

        long l := 123l;
        real r := 123.0r;
        
        int n1 := l; // works
        int n2 := r; // fails        

For unsigned, short or byte integers, both conversions are possible with a loss of information in some cases.

        uint x := 0FFFFFFFEh;
        int  n := 300;
        
        short s := n; // works
        byte  b := n; // works, but the value is 44
        int  sn := x; // works, but the value is -2 

### Mathematical Operations and Functions

ELENA provides a basic set of operations with primitives. It is easily extended with a help of methods declared in appropriate classes and extensions.

#### Arithmetic Operators

The following arithmetic operators are supported on all primitive numeric types:

Expression | Name  
---------- | ---- 
x + y | Addition operator
x - y | Subtraction operator
x * y | Multiplication operator
x / y | Division operator

The operands may be of different types. The result of the arithmetic operation, in this case, is the biggest operand type (byte < short < int < long < real). E.g. when we sum an integer and a floating-point numbers the result is floating-point. The division of two integer types are always an integer. If one of the operands is a floating-point number the division is floating point one. 

    console
       .writeLine(3*4)
       .writeLine(3*4.0r)
       .writeLine(5/2)
       .writeLine(5.0r/2);

The result is:

    12
    12.0
    2
    2.5

Several operation can be used together:

     console.writeLine(1+2*3.5r-3l/2);
 
#### Bitwise Operators

Bitwise operators are supported only by integer numbers.

Expression | Name  
---------- | ---- 
x && y | bitwise **and** operator
x \|\| y | bitwise **or** operator
x ^^ y | bitwise **xor** operator
x $shl y | bitwise shift left operator
x $shr y | bitwise shift right operator

Similar the different integer types can be used and the result is the biggest operand type.

    console
       .writeLine(4 && 2)
       .writeLine(3 || 10000000000l)
       .writeLine(5 ^^ 3)
       .writeLine(5 $shl 2)
       .writeLine(5 $shr 1);

The result is:

    0
    10000000003
    6
    20
    2

#### Assignment Operators

There are four arithmetic assignment operators to simplify the operations with the left hand operands:

Expression | Name  
---------- | ---- 
x += y | add and assign operator
x -= y | subtract and assign operator
x *= y | multiply and assign operator
x /= y | divide and assign operator

For example

    x += y

is a shorthand for
 
    x := x + y

The left-hand operand should always be a variable.

#### Numeric Comparisons

Standard comparison operations are defined for all the primitive numeric types:

Expression | Name  
---------- | ---- 
x == y | equality
x != y | inequality
x < y | less than
x > y | greater than
x <= y | less than or equal to
x >= y | greater than or equal to

The result of a comparison is Boolean values : **true** or **false**.

    import extensions;
      
    public program()
    {
        console
           .printLine(2.0r == 2)
           .printLine(2 == 3)
           .printLine(2 < 3)       
           .printLine(2 > 3)
    }

The results are:

    true
    false
    true
    false

#### Operator Precedence

Operators are ordered in the following manner, from highest precedence to lowest:

Category | Operators
-------- | ---------
Multiplication | *, /, $shl, $shr
Addition | +, -
Comparisons | ==, !=, <, >, <=, >=
Bitwise | &&, \|\|, ^^

#### Numerical Conversions

Numbers can be converted using either implicit or explicit conversions. Implicit conversion is done automatically but part of the information may be lost. Explicit conversions check if the value will be overflown and generate exceptions.

The recommended way is to use conversion extension methods declared in **extensions** module

Method Name | Description
----------- | -----------
toByte()    | converts the target to system'ByteNumber type
toShort()   | converts the target to system'ShortNumber type
toInt()     | converts the target to system'IntNumber type
toUInt()    | converts the target to system'UIntNumber type
toLong()    | converts the target to system'LongNumber type
toReal()    | converts the target to system'RealNumber type

Here several examples:

    import extensions;
    
    public program()
    {
        int n := 3;
        
        byte  b := n.toByte();
        short s := n.toShort();
        long  l := n.toLong();
        real  r := n.toReal();  
    }

If a conversion leads to an overflow an exception is generated:

    int n := 300;
    
    byte  b := n.toByte();

The output will be:

    An index is out of range
    Call stack:
    system'Exception#class.new[1]:exceptions.l(96)
    system'OutOfRangeException#class.new[1]:exceptions.l(126)
    system'OutOfRangeException#class.new[0]:exceptions.l(126)
    extensions'convertorOp.auto#toByte[1]:convertors.l(383)
    mytest'program.#invoke[0]:test.l(16)
    system'$private'entry.#invoke[0]:win32_app.l(37)
    system'#startUp:win32_app.l(55)

#### Methods and functions

The representative classes greatly extends a set of primitive operations. The functionality is declared either in the proper class or in one of its extensions. For convenience the functions can be used as well.

**Operations with bit-wise masks**

Method | Description
------ | -----------
allMask(mask) | Returns true if all the mask bits are set
anyMask(mask) | Returns true if any of the mask bits are set

For examples the code:

    import extensions;
    
    public program()
    {
        console
            .printLine("8.anyMask(15)=", 8.anyMask(15))
            .printLine("8l.allMask(15)=", 8l.allMask(15))
    }

will generate the following result:

    8.anyMask(15)=true
    8l.allMask(15)=false

**Operations with the number sign:**

Method | Description
------ | -----------
Absolute | Returns the absolute value
Inverted | Returns the inverted value
Negative | Returns the negated value

These properties can be used to return negative, positive or inverted values:

    import extensions;
    
    public program()
    {
        int r := -123;
        
        console
            .printLine(r,".Inverted = ", r.Inverted)
            .printLine(r,".Negative = ", r.Negative)
            .printLine(r,".Absolute = ", r.Absolute)
    }

The result is:

    -123.Inverted = 122
    -123.Negative = 123
    -123.Absolute = 123

When we have to check if our number is positive, negative, zero, odd and so on, these extension methods will be useful:

Method | Description
------ | -----------
isOdd() | Returns true if the number is odd, otherwise false           
isEven() | Returns true if the number is even, otherwise false           
isZero() | Returns true if the number is zero, otherwise false
isPositive() | Returns true if the number is positive, otherwise false      
isNegative() | Returns true if the number is negative, otherwise false       
isNonnegative() | Returns true if the number is non negative ( >= 0), otherwise false    

The usage is quite straightforward:

    import extensions;
    
    public program()
    {
        int n := 2;
        
        console
            .printLine(n," is odd : ", n.isOdd())
            .printLine(n," is even : ", n.isEven())
            .printLine(n," is zero : ", n.isZero())
            .printLine(n," is positive : ", n.isPositive())
            .printLine(n," is negative : ", n.isNegative())
    }

with the following result:

    2 is odd : false
    2 is even : true
    2 is zero : false
    2 is positive : true
    2 is negative : false

**Modulo and real division of integer numbers**

Method | Function | Description
------ | -------- | -----------
mod(operand) | modulo(loperand,roperand) | An Integer remainder
realDiv(operand) | A float-based division 

A modulo operation is implemented in ELENA with a help of an extension method **mod**

    import extensions;
    
    public program()
    {
        console.printLine("5 % 2 = ", 5.mod(2))
    }

The result is as expected one:

    5 % 2 = 1

Instead of extension method we may use an appropriate function declared in **extensions'math** module

    import extensions;
    import extensions'math;
    
    public program()
    {
        console.printLine("5 % 2 = ", modulo(5, 2))
    }

A fraction of two integer numbers is always an integer. If we need an exact result we may use **realDiv** extension:

    import extensions;

    public program()
    {
        console.printLine("5 / 2 = ", 5.realDiv(2))
    }

The output will be:

    5 / 2 = 2.5

**Operation with floating-point numbers**

Method | Function | Description
------ | -------- | -----------
Rounded | |  Returns the rounded number           
RoundedInt | | Returns the rounded integer number        
Integer | | Truncates the fraction part
IntegerInt | | Truncates the fraction part and returns an integer 
frac() | frac(operand) | Returns only the fraction part
ceil() | ceil(operand) |  returns the smallest integer that is greater than or equal to the operand
floor() | floor(operand) | the largest integer that is smaller than or equal to the operand
truncate(precision) | truncate(operand,precision) | Rounds the number to the provided precision
Reciprocal | | returns a number obtained by dividing 1 by operand

We may use either extension methods (declared in **system'math**) or functions (declared in **extensions'math**):

    import extensions;
    import system'math;
    import extensions'math;
    
    public program()
    {
        console
            .printLine("foor(5.6)=", floor(5.6r))
            .printLine("ceil(5.6)=", 5.6r.ceil())
    }

The code produces the following output:

    foor(5.6)=5.0
    ceil(5.6)=6.0

**Mathematical functions**

Method | Function
------ | --------
power(operand) | power(loperand,roperand)
sqr() | sqr(operand)
sqrt() | sqrt(operand)
exp() | exp(operand)
ln() | ln(operand)
sin() | sin(operand)
cos() | cos(operand)
tan() | tan(operand)
arctan() | arctan(operand)
arcsin() | arcsin(operand)
arccos() | arccos(operand)
log2() | log2(operand)
log10() | log10(operand)

Similar both extension methods and functions can be used:

    import extensions;
    import system'math;
    import extensions'math;
    
    Pi = RealNumber.Pi;
    
    public program()
    {
        console
            .printLine("sin(π/3) = ",(Pi / 3).sin())
            .printLine("cos(π/3) = ",cos(Pi / 3))
            .printLine("tan(π/3) = ",tan(Pi / 3))
    }

The result is:

    sin(π/3) = 0.8660254037844
    cos(π/3) = 0.5
    tan(π/3) = 1.732050807569

**Converting to radians, degrees**

Method | Description
------ | -----------
Radian | Converts to radians
Degree | Converts to degree

### Strings

**String** is a sequence of characters representing some text information. The way how to interpret these characters depends on the text encoding. ELENA supports both UTF-8 (**system'String**, or **string** alias) and UTF-16  (**system'WideString**,or **wide** alias) encoded strings. 

Strings are immutable. To modify its content a new string should be created.

A string can be considered as a partial array of UTF-32 characters. It means that for some indexes no character can be returned (an exception will be generated). The reason is UTF-8 / 16 encoding (the character may take more more than one element in the array). When our text is plain ASCII it makes no difference. But for example Russian (for a string) or Chinese (for a wide string) we have to take this feature into account.

#### Characters

A character is a 32-bit primitive value represented by **system'CharValue** class (**char** alias). It can be converted into a 32 bit integer number and vice versa. It supports comparison operations both with another characters and with numbers.

    import extensions;
     
    public program()
    {
        auto s := "♥♦♣♠";    
        
        char ch := s[0];
        int  code := ch.toInt();
        
        console.printLine(ch);
        console.printLine(code);
        console.printLine(ch == code);
    }

The result is:

    ♥
    9829
    true

ELENA supports character literal constant as well:

    console.printLine($9829); // character literal

which will be printed like this:

    ♥

#### String literals

The string literals are enclosed in double quotes:

    string stringConstant := "Привет Мир"; // UTF-8 encoded constant
    wide wideConstant   := "你好世界"w;     // UTF-16 encoded constant

If the string itself contains double quote it should be doubled:

    console.printLine("The string may contain a symbol """);

The output is:

    The string may contain a symbol "

Multi-line string is supported as well:

    console.printLine("This is a string with
    two lines");

The result:

    This is a string with
    two lines

An empty string may be a string literal without content or a constant **emptyString**:

    console.printLine("");
    console.printLine(emptyString);

#### Reading string content

If you want to extract a character from a string, you index into it using an array operator. The index of the first element is 0.

Let's print a first, second and the last characters of the given string. As it was said above for the plain English text, it is quite straight-forward:

    import extensions;
    
    public program()
    {
        auto s := "Hello";    
        
        console.printLine(s[0]); // printing the first element
        console.printLine(s[1]); // printing the second element
        console.printLine(s[s.Length - 1]); // printing the last element
    }

The output is:

    H
    e
    o

Let's try it with Russian word:

    auto s := "Привет";    
    
    console.printLine(s[0]); // printing the first element
    console.printLine(s[1]); // printing the second element
    console.printLine(s[s.Length - 1]); // printing the last element

There error is raised for the second element:

    П
    An index is out of range
    Call stack:
    system'Exception#class.new[1]:exceptions.l(96)
    system'OutOfRangeException#class.new[1]:exceptions.l(126)
    system'OutOfRangeException#class.new[0]:exceptions.l(126)
    system'String.at[1]:memory.l(1356)
    mytest'program.#invoke[0]:test.l(8)
    system'$private'entry.#invoke[0]:win32_app.l(37)
    system'#startUp:win32_app.l(55)

Why? Because in UTF-8 a russian character is encoded with two bytes (two elements of String array). It means that the second character must be read from third, and not from the second position.

We may actually fix the problem using UTF-16. Now every symbol is encoded in only one array element.

    auto s := "Привет"w;    
    
    console.printLine(s[0]); // printing the first element
    console.printLine(s[1]); // printing the second element
    console.printLine(s[s.Length - 1]); // printing the last element

And the output is:

    П
    р
    т

But for the Chinese word it will not work.

To correctly read the next character, we have to use a character **Length** (for strings) or **WideLength** (for wide strings) properties:

    auto s1 := "Привет";    
    auto s2 := "你好世界"w;    
    
    console.printLine(s1[s1[0].Length]);     // printing the second element of UTF-8 string
    console.printLine(s2[s2[0].WideLength]); // printing the second element of UTF-16 string

Enumerators can be used to simplify the task:

    auto s := "Привет";    
    auto it := s.enumerator();   // creating a string enumerator

    it.next();                   // going to the first element
    console.printLine(it.get()); // printing the current element  
    it.next();                   // going to the second element
    console.printLine(it.get()); // printing the current element  

The output as expected:

    П
    р

To read the last one we have go to the end one by one. Instead let's use an extension method **LastMember** declared in **system'routines** module:

    import extensions;
    import system'routines;
 
    public program()
    {
        auto s1 := "Привет";    
        
        console.printLine(s1.LastMember);
    }

The output is

    т

#### Basic operations and functions

You can compare two strings:

    console.print("""string1"" < ""string1""=", "string1" < "string2");
    console.print("""string1"" > ""string1""=", "string1" > "string2");
    console.print("""string1"" == ""string1""=", "string1" == "string2");

with the following result:

    "string1" < "string1"=true
    "string1" > "string1"=false
    "string1" == "string1"=false

Several strings or a string and a char can be concatenated. The result of the operation is always a new string.

    import extensions;
    
    public program()
    {
        console.print("Hello" + " " + " World " + $10);
    }

The result will be

    Hello  World 

We can insert a sub string into or delete it from given string:

    console.printLine("Hillo World".delete(1,1).insert(1, "e"));

The output is:

    Hello World

The first operand is a position in the given string (*note that we may corrupt the string if a sub string will be inserted into multi-element character*). The second one is the length of the sub string for **delete** and a sub string for **insert**.

We may find a position of a sub string in the given one.

    console.printLine("Hello World".indexOf(0, " "));
    console.printLine("Hello World".indexOf(0, "!"));

The result will be

     5
     -1

The first operand is starting index and the second one is the sub string to be found. The method will return the index of the first occurrence of the second argument or -1. 

We may return a sub string from the given one:

    console.printLine("Hello World".Substring(6,5));

and the result is:

    World

Similar to the examples above the first index is a position of the sub string and the second one is its length.

The exception will be generated if the index lays outside the target string for these operations.

There is a number of extension methods for formatting, padding and interpolating strings