# Interpose dynamic library calls in Linux and OS X

I recently discovered how easy it is to _interpose_ (intercept) dynamic library in Linux and OS X. It may be a breeze in Windows too&mdash;I haven't looked.

The process goes like this:

1. Choose a function to interpose.
2. Find the function signature.
3. Create a custom version of the function.
4. Compile a shared library.
5. Pre&ndash;load the library.

I will discuss each step in detail.

## Step 1: Choose a function to interpose

In this tutorial, we'll be messing with the output of `uname`. Here's the normal output on my MacBook:
```
$ uname -v
Darwin Kernel Version 10.8.0: Tue Jun  7 16:33:36 PDT 2011; root:xnu-1504.15.3~1/RELEASE_I386
```

What library calls does `uname` make? Let's find out by listing all of the undefined symbols contained in the binary:
```
$ nm -uj "$(which uname)"
___stack_chk_guard
___stderrp
___stdoutp
_compat_mode
_err
_exit
_fputs
_fwrite
_getenv
_getopt
_optind
_putchar
_setlocale
_strncpy
_uname
dyld_stub_binder
```

Most of these are probably function calls, but `_uname` looks like what we're interested in. OS X requires the leading underscore, so the function we really want is `uname`.


## Step 2: Find the function signature

Let's check the man page for the `uname` function:
```
$ man 3 uname
```
```
NAME
     uname -- get system identification

LIBRARY
     Standard C Library (libc, -lc)

SYNOPSIS
     #include <sys/utsname.h>

     int
     uname(struct utsname *name);
:
:
```

From this, we now know that the function signature is:
```C
int uname(struct utsname *name);
```


## Step 3: Create a custom version of the function

We'll create a custom `uname` function which calls the original, then modifies the results (`interpose_uname.c`):
```C
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
```

Without the comments the code is fairly short and most of it is boiler&ndash;plate.


## Step 4: Compile a shared library

Linux:
```
$ gcc -shared -fPIC -Wall -Werror -std=c99 -o libinterpose_uname.dylib interpose_uname.c
```

OS X:
```
$ gcc -shared -fPIC -Wall -Werror -std=c99 -o libinterpose_uname.so interpose_uname.c
```


## Step 5: Pre&ndash;load the library

Finally, the fruits of our labor. Let's run `uname -v` again, this time with our custom library pre&ndash;loaded.

Linux:
```
$ DYLD_FORCE_FLAT_NAMESPACE=1 DYLD_INSERT_LIBRARIES=libinterpose_uname.dylib uname -v
Johnny 5
```

OS X:
```
$ LD_PRELOAD=libinterpose_uname.so uname -v
Johnny 5
```


## Automate

In **Step 3** I had mentioned that the interposing code is mostly boiler&ndash;plate. With that in mind, I wrote a [small utility to automatically generate the interposing code](https://github.com/themattrix/interpose) from a header. The generated code is C++(11) instead of C, so we can have some more fun with it. Let's duplicate the above example with this new utility:
```
$ git clone git://github.com/themattrix/interpose.git
$ cd interpose/src
$ DEST=.                                # Output generated content to this directory
$ HEADER=/usr/include/sys/utsname.h     # Generate code from this header
$ API_LIB=/usr/lib/libc.dylib           # OS X only: library containing original uname()
$ make interpose-src                    # Generate code
$ make interpose-lib                    # Compile code
$ make do-interpose APP='uname -v'      # Interpose

=================================================[ Running interposing code ]===
[1338738075.588706][done][0.000056] uname()
Darwin Kernel Version 10.8.0: Tue Jun  7 16:33:36 PDT 2011; root:xnu-1504.15.3~1/RELEASE_I386
```

By default, the generated code (`interpose_usr_utsname.cpp`) timestamps the function calls:
```C++
template<typename Function>
auto uname(Function original, struct utsname *arg1) -> int
{
   return timestamp(original(arg1));
}
```

Instead, let's match the functionality of our C code:
```C++
template<typename Function>
auto uname(Function original, struct utsname *name) -> int
{
   int result(original(name));

   if(result == 0)
   {
      // just so I don't have to say name->vesion several times
      auto &v(name->version);

      // copy "Johnny 5" into name->version
      v[std::string("Johnny 5").copy(v, sizeof(v))] = '\0';
   }

   return result;
}
```

Let's see how we did:
```
$ make interpose-lib
$ make do-interpose APP='uname -v'

=================================================[ Running interposing code ]===
Johnny 5
```

Perfect!


## Resources

1. [Tutorial: Function Interposition in Linux](http://www.jayconrod.com/cgi/view_post.py?23)
2. [Overriding library functions in Mac OS X, the easy way: DYLD_INSERT_LIBRARIES](http://tlrobinson.net/blog/2007/12/overriding-library-functions-in-mac-os-x-the-easy-way-dyld_insert_libraries/)
