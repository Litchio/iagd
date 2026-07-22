/*
 * MinGW compatibility shim for Microsoft Structured Exception Handling keywords.
 * Maps __try/__except to standard C++ try/catch for compilation with GCC/MinGW.
 * The semantic difference is acceptable: Detours only uses SEH to return
 * false on access violations, which is the same behavior as catching all exceptions.
 */
#ifdef __GNUC__
#  define __try        try
#  define __except(x)  catch(...)
#  define __finally
/* Suppress MSVC pragmas that GCC doesn't know about */
#  pragma GCC diagnostic ignored "-Wunknown-pragmas"
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
