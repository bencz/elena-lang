import system'text;
import system'collections;
import system'routines;

singleton stringExConvertor
{
//    string convert(RealMatrix a)
//        = realmatrixExConvertor.toString(a);
//
//    string convert(IntMatrix a)
//        = intmatrixExConvertor.toString(a);

   string convert(Indexer it)
   {
      auto output := new TextBuilder();
      while (it.Available)
      {
         output.write((*it).toString());
         output.write:",";
            
         it.appendIndex(1)
      };
        
      int length := output.Length;
      if (0 < length)
         { output.Length := -1 + length };
        
      ^ output
   }

   string convert(Enumerator en)
   {
      auto output := new TextBuilder();
      while(en.next())
      {
         output.write((*en).toString());
         output.write:",";
      };

      int length := output.Length;
      if (0 < length)
         { output.Length := -1 + length };
        
      ^ output
   }

//    string convert(Date val) = val.toPrintable();
//        
//    string convert(Timespan val) = val.toPrintable();

   string convert(Enumerable o)
      <= convert(o.enumerator());

   string convert(Indexable o)
      <= convert(o.indexer());

   string convert(o)
      = stringConvertor.convert(o);
}

// --- arrayConvertorEx ---

public singleton arrayConvertorEx
{
   Array convert(o)
   {
      var arr := cast Array(o) ?? nil;
      if (arr == nil)
      {
         var it := o.indexer();
         int len := it.Length;
         arr := Array.allocate(len);
         for (int i := 0, i < len, i += 1)
         {
            arr[i] := *it;
            it.appendIndex(1)
         }
      };
        
      ^ arr
   }
    
   Array convert(Enumerator en)
   {
      ArrayList retVal := new ArrayList();

      en.forEach:(x){ retVal.append:x };

      ^ retVal
   }

   Array convert(Array arr)
      = arr;
}

// --- enumerableConvertor ---

singleton enumerableConvertor
{
   Enumerable convert(Enumerable o)
      = o;
    
   Enumerable convert(o)
      = new Enumerable { Enumerator enumerator() = o.enumerator(); };
}

// --- convertOp ---

public extension convertOp
{
   convertTo(target)
      = new Variant(self).saveTo(target);   
}

// --- stringConvertor ---

public extension stringConvertOp
{
   string toString()
      = stringExConvertor.convert(self);

   string toString(int radix)
      = stringConvertor.convert(self, radix);
}

// --- intConvertOp ---

public extension intConvertOp
{
   int toInt()
     = intConvertor.convert(self);

   int toInt(int radix)
     = intConvertor.convert(self, radix);
}

// --- byteConvertOp ---

public extension byteConvertOp
{
   byte toByte()
     = byteConvertor.convert(self);
}

// --- shortConvertOp ---

public extension shortConvertOp
{
   short toShort()
     = shortConvertor.convert(self);
}

// --- longConvertOp ---
public extension longConvertOp
{
   long toLong()
      = longConvertor.convert(self);
}

// --- realConvertOp ---
public extension realConvertOp
{
   real toReal()
      = realConvertor.convert(self);
}

// --- enumerableExOp ---
public extension enumerableExOp
{
    Enumerable asEnumerable()
        = enumerableConvertor.convert(self);
}

// --- arrayConvertOp ---
public extension arrayConvertOp
{
   Array toArray()
      = arrayConvertorEx.convert(self);

}

// --- convertorOp ---

public extension convertorOp
{
   convertTo(target)
      = new Variant(self).saveTo(target);
}