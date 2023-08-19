// --- base inline templates ---

public inline external(routine)
{
   #include intern routine;
}

// --- class meta info ---

public inline info(descr)
{
   #new super string descriptions;

   #let super descriptions[$name __decl] := descr;
}

// --- preloaded ---

public inline preloaded()
{
   #new __string preloadedSymbols;

   #let preloadedSymbols += __decl;
}

// --- prop ---

public field prop<name, T>
{
   get T name()
   {
      ^ name
   }

   set name(T value)
   {
      name := value
   }
}

// --- rprop ---

public field rprop<name, T>
{
   get T name()
   {
      ^ name
   }
}

// --- interface<T> ---

public interface<T>
{
   T cast() = new T
   {
      embeddable dispatch() => self;
   };
}