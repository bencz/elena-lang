import extensions;

class Extender : BaseExtender
{
    prop object foo;
    
    constructor(object)
    {
        theObject := object
    }
}

public program()
{
    var object := 234;
  
    // extending an object with a field
    object := new Extender(object);

    object.foo := "bar";

    console.printLine(object,".foo=",object.foo);

    console.readChar()
}