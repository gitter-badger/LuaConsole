
/*
	
	Lua.c Console
		- Line by Line interpretation
		- Files executed by passing
		- Working directory support
		- Built in stack-dump
	
	Works with:
		5.3.x
	
*/

#if defined(__MINGW32__) || defined(__linux__) || defined(__unix__)
#define CHDIR_FUNC chdir
#include <unistd.h>
#else
#define CHDIR_FUNC _chdir
#include <dirent.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "additions.h"


// internal enums, represent lua error category
typedef enum LuaConsoleError {
	INTERNAL_ERROR = 0,
	SYNTAX_ERROR = 1,
	RUNTIME_ERROR = 2,
} LuaConsoleError;


// usage message
const char HELP_MESSAGE[] = 
	"Lua.c Console Updated 4/29/2017\n"
	"\n"
	"\t- Line by Line interpretation\n"
	"\t- Files executed by passing\n"
	"\t- Working directory support\n"
	"\t- Built in stack-dump\n"
	"\n"
	"Usage: lua.exe [FILE_PATH] [-v] [-e] [-s START_PATH] [-p] [-?] \n"
	"\n"
	"-v \t Prints the Lua version in use\n"
	"-e \t Prevents lua core libraries from loading\n"
	"-s \t Issues a new root path\n"
	"-p \t Has console post exist after script in line by line mode\n"
	"-a \t Removes the additions\n"
	"-? \t Displays this help message\n";



// one environment per process
static lua_State* L;


// variable to end line by line iterpretation loop
static int should_close = 0;

// necessary to put this outside of main, print doesn't work
static int no_libraries = 0;




// comprehensive error output
static void print_error(LuaConsoleError error) {
	const char* msg = lua_tostring(L, 1);
	switch(error) {
	case INTERNAL_ERROR:
		printf("(Internal)");
		break;
	case SYNTAX_ERROR:
		printf("(Syntax)");
		break;
	case RUNTIME_ERROR:
		printf("(Runtime)");
		break;
	}
	int top = lua_gettop(L);
	fprintf(stderr, " | Stack Top: %d | %s\n", lua_gettop(L), msg);
	if(top > 1) // other than error message
		stackDump(L);
	lua_pop(L, 1);
}


// handles line by line interpretation
static int lua_main_postexist(lua_State* L) {
	char* buffer = calloc(1025, 1);
	if(buffer == 0) {
		fprintf(stderr, "%s\n", "Allocation Failed: Out of Memory");
		exit(1);
	}
	char* buffer2 = calloc(1033, 1);
	if(buffer2 == 0) {
		fprintf(stderr, "%s\n", "Allocation Failed: Out of Memory");
		exit(1);
	}
	
	int status = 0;
	while(should_close != 1) {
		// reset
		status = 0;
		memset(buffer, 0, 1025);
		memset(buffer2, 0, 1033);
		
		// read
		fputs(">", stdout);
		fgets(buffer, 1024, stdin);
		buffer[strlen(buffer)-1] = '\0';
		snprintf(buffer2, 1032, "return %s;", buffer);
		
		// do first test
		status = luaL_loadstring(L, buffer2);
		if(status != 0) {
			lua_pop(L, 1);
		} else {
			// attempt first test
			int top = lua_gettop(L);
			status = lua_pcall(L, 0, LUA_MULTRET, 0);
			if(status == 0) { // on success
				top = lua_gettop(L) - top + 1;
				if(top > 0) {
					if(no_libraries == 1) {
						lua_pop(L, top);
						continue;
					}
					lua_getglobal(L, "print");
					lua_insert(L, lua_gettop(L)-top);
					lua_call(L, top, 0);
				}
				continue;
			}
			lua_pop(L, 1);
		}
		
		// do originally inserted code
		status = luaL_loadstring(L, buffer);
		if(status != 0) {
			print_error(SYNTAX_ERROR);
			continue;
		}
		
		// attempt originally inserted code
		status = lua_pcall(L, 0, 0, 0);
		if(status != 0) {
			print_error(RUNTIME_ERROR);
		}
	}
	
	free(buffer);
	free(buffer2);
	
	return 0;
}

// handles the execution of a file.lua
static int lua_main_dofile(lua_State* L) {
	const char* file = lua_tostring(L, 1);
	lua_pop(L, 1);
	if(luaL_loadfile(L, file) != 0) {
		print_error(SYNTAX_ERROR);
		return 0;
	}
	if(lua_pcall(L, 0, 0, 0) != 0) {
		print_error(RUNTIME_ERROR);
		return 0;
	}
	return 0;
}



// all-in-one function to handle file and line by line interpretation
int start_protective_mode(lua_CFunction func, const char* file) {
	lua_pushcfunction(L, func);
	int status = 0;
	if(file == 0)
		status = lua_pcall(L, 0, 0, 0);
	else {
		lua_pushlstring(L, file, strlen(file));
		status = lua_pcall(L, 1, 0, 0);
	}
	if(status != 0) {
		print_error(INTERNAL_ERROR);
		return 1;
	}
	return 0;
}

// handles arguments, cwd, loads necessary data, executes lua
int main(int argc, char* argv[])
{
	int print_version = 0;
	int change_start = 0;
	int post_exist = 0;
	int no_file = 0;
	char* start = 0;
	int no_additions = 0;
	
	
	// handle arguments
	if(argc == 1) { // post-exist if !(arguments > 1)
		post_exist = 1;
		no_file = 1;
	} else {
		// don't try to execute file if it isn't first argument
		if(argv[1][0] == '-')
			no_file = 1;
		int i;
		for (i=1; i<argc; i++) {
			// don't parse non-switches
			switch(argv[i][0]) {
			case '/':
			case '-':
				break;
			default:
				continue;
			}
			// set variables up for later parsing
			switch(argv[i][1]) {
			case 'v': case 'V':
				print_version = 1;
				break;
			case 'e': case 'E':
				no_libraries = 1;
				break;
			case 's': case 'S':
				change_start = 1;
				start = argv[i+1];
				break;
			case 'p': case 'P':
				post_exist = 1;
				break;
			case 'a': case 'A':
				no_additions = 1;
				break;
			case '?':
				fprintf(stdout, "%s\n", HELP_MESSAGE);
				return 0;
			}
		}
	}
	
	
	// make sure to start in the requested directory, if any
	if(change_start == 1 && CHDIR_FUNC(start) == -1) {
		fprintf(stderr, "%s\n", "Invalid start directory supplied");
		fprintf(stdout, "%s\n", HELP_MESSAGE);
		return 1;
	}
	
	// initiate lua
	L = luaL_newstate();
	if(L == 0) {
		fprintf(stderr, "%s\n", "Allocation Failed: Out of Memory");
		return 1;
	}
	
	// initiate the libraries
	if(no_libraries == 0)
		luaL_openlibs(L);
	
	// print out the version, new state because no_libraries can be 1
	if(print_version == 1) {
		lua_State* gL = luaL_newstate();
		if(gL == 0) {
			fprintf(stderr, "%s\n", "Allocation Failed: Out of Memory");
			return 1;
		}
		
		luaL_openlibs(gL);
		
		// print lua version
		lua_getglobal(gL, "_VERSION");
		fprintf(stdout, "%s\n", lua_tostring(gL, 1));
		lua_pop(gL, 1);
		lua_close(gL);
	}
	
	
	// if there is nothing to do, then exit, as there is nothing left to do
	if(no_file == 1 && post_exist != 1) {
		lua_close(L);
		return 0;
	}
	
	
	// add additions
	if(no_additions == 0)
		additions_add(L);
	
	
	// load function into protected mode (pcall)
	int status = 0;
	if(post_exist == 1) {
		if(no_file == 0)
			status = start_protective_mode(&lua_main_dofile, argv[1]);
		status = start_protective_mode(&lua_main_postexist, (const char*) 0);
	} else status = start_protective_mode(&lua_main_dofile, argv[1]);
	lua_close(L);
	
	return status;
}
