#include <sys/utsname.h>   // struct utsname
#include <dlfcn.h>         // dlsym(), dlopen() [OS X]
#include <stdio.h>         // printf()
#include <stdlib.h>        // exit()
#include <string.h>        // strcpy()

int uname(struct utsname *name) 
{
   typedef int (*func_t)(struct utsname *);

   static func_t original = NULL;

   if(original == NULL)
   {
#ifdef __APPLE__
      /** 
       ** On OS X, the original library is loaded explicitly and the function is
       ** queried from within that library. This technique does not work on Linux; it
       ** results in an infinite recurse.
       **/

      // grab handle to the original library
      void *handle = dlopen("libc.dylib", RTLD_NOW);

      // find the original function within that library
      original = (func_t)dlsym(handle, __func__);
#else
      /** 
       ** Retrieving a pointer to the original function is even easier in Linux. It
       ** doesn't even require the original library name. Calling dlsym() with the
       ** flag "RTLD_NEXT" returns the *next* occurrence of the specified name, which
       ** is the original library call. This does not work on OS X; it fails to find
       ** the function.
       **/

      // find the original function
      original = (func_t)dlsym(RTLD_NEXT, __func__);
#endif

      if(original == NULL)
      {
         printf("ERROR: Failed to locate original %s() function; exiting\n", __func__);
         exit(1);
      }
   }

   // finally call the original uname()   
   int result = original(name);

   // if successful, change some of the results
   if(result == 0)
   {
      // <sys/utsname.h> defines 'struct utsname' as follows:
      //
      // #define  _SYS_NAMELEN   256
      //
      // struct   utsname {
      //    char  sysname[_SYS_NAMELEN];  /* [XSI] Name of OS */
      //    char  nodename[_SYS_NAMELEN]; /* [XSI] Name of this network node */
      //    char  release[_SYS_NAMELEN];  /* [XSI] Release level */
      //    char  version[_SYS_NAMELEN];  /* [XSI] Version level */
      //    char  machine[_SYS_NAMELEN];  /* [XSI] Hardware type */
      // };
      //
      // Let's change the version to "Johnny 5".
      //
      strncpy(name->version, "Johnny 5", sizeof(name->version));
   }

   // return the original result, unchanged
   return result;
}
