#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <math.h>


#define BUFSIZE 1024
#define IDCOUNT 50
#define IDSIZE 12
#define TEMPNAME "temp.c"

// Prints a standard commandline utility program usage message.
// If `err`, the usage message is printed as an error, and the
// program terminates with EXIT_FAILURE. 
void usage(bool err) {

    // If --help is the only cmd argument
    FILE * output = stdout;
    char char_one = '@';
    int exit_status = EXIT_SUCCESS;

    if(err){
        output = stderr;
        char_one = '!';
        exit_status = EXIT_FAILURE;
    }

    fprintf(output, "%c Usage: ./runml.out path/to/file.ml arg0 arg1 arg2 [...] \n", char_one);
    fprintf(output, "%c arg0, arg1, ... must be integers (e.g. 1, 3000, ...), ", char_one);
    fprintf(output, "or floating-point numbers (e.g. 3.1415). \n");
    fprintf(output, "%c Args can be signed. \n", char_one);
    exit(exit_status);
}

// closes active files, removes temp files created
// make this variadic, so you can close as many files as you want at any given instance
void closeFiles(FILE * c_fp, FILE * ml_fp) {
    fclose(c_fp);
    fclose(ml_fp);
    remove(TEMPNAME);
}

// splits `line` into substrings seperated by `sep`
// places pointers to each of the substrings in `buffer`
// I FUCKING HATE STRTOK!!!!!! I HATE IT!!!!
void splitByDelim (char *line, char *buffer[BUFSIZE], char sep[]) {
    char *temp;
    strcpy(temp, line);
    char *token;
    token = strtok(temp, sep);
    int i = 0;
    while (token != NULL) {
        buffer[i] = token;
        token = strtok(NULL, sep); 
        i++;
    }
    // insert a string representing a null byte, 
    // so we know when the array ends
    buffer[i] = "\0";
}

struct scope_ids {
    char *ids[IDCOUNT]; // an array of identifier strings for this scope
    int id_count; // the number of identifiers added to this scope
};

// Stores required data structures during c file construction
struct scope_info {
    // a list of identifiers in the global scope, 
    // and the count of identifiers added to the global scope
    struct scope_ids glo_scope; 
    // a list of identifiers in the immediate function scope
    // is reconstructed when function declaration is encountered
    struct scope_ids func_scope;
    // true if inside a local function, and false if 
    // in global scope
    bool in_func;
};

struct func_attrs {
    char *type; // a string to the return type of the current function
    char *header; // the function identifier and arguments
    char *identif; // the name of the function - added to global scope once function terminates
    char **body; // pointer to array of lines
    int size; // number of lines allocated for this function
    int linecount; // number of lines of this function's body
} def_func = {.type = "void", .size = 4, .linecount = 0};




bool idInScope(char *iden, struct scope_info *scopes) {
    if(scopes->in_func) { // we can check function scope too
        for(int i = 0; i < scopes->func_scope.id_count; i++) {
            // printf("func scope ids: %s \n", scopes->func_scope.ids[i]);
            if(!strcmp(iden, scopes->func_scope.ids[i])) {
                return true;
            }
        }
    } // if the loop terminates or we're not currently  
    // in function scope, check global scope too
    for(int i = 0; i < scopes->glo_scope.id_count; i++) {
        if(!strcmp(iden, scopes->glo_scope.ids[i])) {
            return true;
        }
    }
    // if we still haven't encountered anything
    return false;
}

// adds required headerfiles
void addPreDirectives(char * argv[], FILE * c_fp, int argc) {
    // adds this line so we may call `printf`
    char include[] = "#include <stdio.h> \n";
    // Ensures correct types when printing
    char print_macro[] = "#define print(a) (int) a == a ? fprintf(stdout, \"%d\\n\", (int) a): fprintf(stdout, \"%6f\\n\", a); \n";
    
    // a char array to contain all required preprocessor directives
    // `&cmd_args - ptr` returns the length of `cmd_args` 
    char pre_dir[strlen(include) + strlen(print_macro)];
    sprintf(pre_dir, "%s%s", include, print_macro);
    if (fputs(pre_dir, c_fp) == EOF) {
        fprintf(stderr, "@ Unexpected error writing to file %s \n", TEMPNAME);
    }
}

// defines `argname` `value` at the top of TEMPNAME
void defineArg(char argname[], char value[], FILE * c_fp) {
    char directive[BUFSIZE];
    // defines all commandline arguments at top of temporary C file.
    sprintf(directive, "#define %s %s\n", argname, value);
    if (fputs(directive, c_fp) == EOF) {
        fprintf(stderr, "@ Unexpected error writing to file %s \n", TEMPNAME);
    }
}

bool isNumerical(char maybe_number[]) {
    int per_count = 0; // stores the number of periods encountered
    int iterations = 0;
    char *j = maybe_number;
        if (*j == '-') j++; // skip the negative sign
        
    for(; *j != '\0'; j++, iterations++) {
        if (*j == '.') {
            // if we've already encountered '.', this can't be valid
            if (per_count) {
                return false;
            }
            per_count++; // increment per_count otherwise
        } else if (!isdigit(*j)) { // non-numeric char
            return false;
        }
    }
    if (!iterations){
        return false; // we have a weird input like " - "
    }
    // survived the tests; it's a number
    return true;
}

// iterates through argv, adding valid arguments to `scope_ids[0]`, and terminating 
// if any arguments are non-numerical
void validateArgv(int argc, char * argv[], struct scope_info *scopes, FILE * c_fp) {
    for(int i = 0; i < argc - 2; i++) {
        // checks if the current argument string is a number
        if (!isNumerical(argv[i + 2])) { 
            // non-numeric arg encountered - program terminates accordingly.
            fclose(c_fp);
            remove(TEMPNAME);
            usage(true);
        }

        // ASSUMPTION - our arg{number} ID mustn't exceed IDSIZE
        // e.g.: we aren't going to get arg100000000. 
        char *argname = malloc(IDSIZE + 1); 
        sprintf(argname, "arg%d", i);
        // Adds the identifier name to the global scope
        // increments the number of global scope identifiers
        scopes->glo_scope.ids[i] = argname;
        scopes->glo_scope.id_count += 1;

        // writes this arg to the top of the temporary c file
        defineArg(argname, argv[i + 2], c_fp);
    }
}

// adds an identifier to a scope depending on the active scope, 
// and increments the number of identifiers in that scope. 
void addToScope(struct scope_info *scopes, char * iden) {

    // NEED TO COPY THE VALUES OF THE POINTERS TO ALLOCATED STRINGS
    // WITH DIFFERENT ADDRESSES!! Otherwise they get overwritten???
    char *temp = malloc(strlen(iden));
    strcpy(temp, iden);

    if(scopes->in_func) {
        int count = scopes->func_scope.id_count; //readability
        // scopes->func_scope.ids[count] = malloc(strlen(iden) + 1);
        scopes->func_scope.ids[count] = temp;
        scopes->func_scope.id_count++;
        // printf("Please god %s \n", scopes->func_scope.ids[count]);
    } else {
        int count = scopes->glo_scope.id_count; //readability
        scopes->glo_scope.ids[count] = temp;
        scopes->glo_scope.id_count++;
    }
}

// works
void constructFuncHeader(char *substrs[], struct scope_info *scopes, struct func_attrs *func) {
    char argbuffer[BUFSIZE*2]; // because we're adding commas, a type, and a space to each argument
    argbuffer[0] = '\0'; // incase the function has no parameters
    char * argpos = argbuffer;
    int j = 0;
    for(int i = 2; strcmp(substrs[i], "\0"); i++, j++) {
        // writes a parameter to argbuffer at position argpos
        sprintf(argpos, "float %s, ", substrs[i]); 
        // also write the function parameter to the function's scope
        addToScope(scopes, substrs[i]);
        int pain = scopes->func_scope.id_count;
        argpos += 8 + strlen(substrs[i]); // 8 for "float , "
        j += 8;
    }
    argbuffer[j - 2] = '\0'; // chops off the last comma onwards
    char *function_header = malloc(BUFSIZE);
    // everything but the return type
    sprintf(function_header, "%s(%s) { \n", substrs[1], argbuffer); 
    // return function_header;
    char * identif = malloc(strlen(substrs[1]));
    // need to copy it!!!
    // Yep, that was it, it was referring to a bit of the string `line` which was 
    // being overwritten
    strcpy(identif, substrs[1]);
    func->identif = identif;
    printf("IDENTIF : %s \n", substrs[1]);
    func->header = function_header;
}

// Looks through an array from a specified starting index (startpos)
// and substitutes undefined identifiers with `0.0`, before returning the array
// Substitutes `0.0` for undeclared variables in these cases:
//    identifier "<-" expression
// |  print  expression
// |  return expression
// replaces any undeclared identifiers with 0.0, and returns the expression. 
char * substituteArgs(int start, char *line, struct scope_info *scopes) {
    // case handling
    if(line[start] == '\0' || line[start] == '\n' || line[start] == '#') {
        return "\0"; // ignore this substring
    }
    char *outstr = malloc(BUFSIZ);
    int j = 0;
    // might have to do *i = line, then add start to i
    // MIGHT GET AN ERROR HERE
    int len = scopes->func_scope.id_count;
    for(char *i = &line[start]; *i != '\0' && *i != '\n'; i++) {
        if(isalnum(*i)) {
            char iden[IDSIZE];
            // adds chars to iden until an identifier is encountered
            int k = 0;
            for(; isalnum(*i) && *i != '\0' && *i != '\n'; k++, i++) {
                iden[k] = *i;
            } 
            iden[k] = '\0'; // making sure it's well behaved
            // once this loop terminates, we have an identifier
            char *outpos = outstr;
            outpos += j;
            if(isNumerical(iden) || idInScope(iden, scopes)) {
                // we're safe to add it to our output string
                sprintf(outpos, "%s", iden); 
                j += k;
                // seems dodgy
            } else{
                if(k > 2) {
                    if(isdigit(iden[3])) { // we have an `arg{number}`, e.g. a cmd arg
                        sprintf(outpos, "%s", iden);
                        j += k;
                    } else {
                        outstr[j] = '0';
                        j++;
                    }
                } else { // id not in scope - substitute zero 
                    outstr[j] = '0';
                    j++;
                }
            } 
            i--; // so we don't get ahead of ourselves in the loop above
        } else if (*i == '#') {
            return outstr; // we must have a valid line
        } else { // we have '(', ')', '+', etc. we can add this to our output str
            outstr[j] = *i;
            j++;        
        }
    }
    return outstr;
}

void addFuncLine (struct func_attrs *func, char *line) {
    // what're you adding??
    if(func->linecount == 0) { // construct new 2d array
        func->body = malloc(4 * sizeof(char*));
    }
    int lines = func->linecount;
    if(lines == func->size) { // need to increase size
        func->size *= 2;
        // chars are universally one byte
        char **newbody = malloc(func->size * 2);
        for(int i = 0; i < lines; i++) {
            newbody[i] = func->body[i];
        }
        free(func->body);
        func->body = newbody;
    }
    // the add step
    // for some reason it forgets the fucking line
    func->body[lines] = line;
    func->linecount += 1;
}

// writes a completed function to the c file 
void writeFunc(struct func_attrs *func, FILE * c_fp) {
    char func_line[BUFSIZE];
    // writes `{type} {identifier}({args}) { \n` to `func_line`
    sprintf(func_line, "%s %s", func->type, func->header);
    if (fputs(func_line, c_fp) == EOF) { // writes function header to C file
        fprintf(stderr, "@ Unexpected error writing to file %s \n", TEMPNAME);
    }
    for(int i = 0; i < func->linecount; i++) {
        printf("%s \n", func->body[i]);
        if (fputs(func->body[i], c_fp) == EOF) { // writes each line of the function
            fprintf(stderr, "@ Unexpected error writing to file %s \n", TEMPNAME);
        }
    }
}

void assignment(char *substrs[], struct scope_info *scopes, char line[], char *outline) {
    addToScope(scopes, substrs[0]); // adds the assigned identifier to the active scope ids
    int shift = (scopes->in_func)? 8:4;
    char *assign_exp = substituteArgs(strlen(substrs[0]) + shift, line, scopes);
    sprintf(outline, "float %s = %s;\n", substrs[0], assign_exp); 
}

// handles statements (excluding return)
void handleStat(char *substrs[], struct scope_info *scopes, char *line, struct func_attrs *func) {
    // char outline[BUFSIZE];

    char *outline = malloc(BUFSIZE);
    if (!strcmp(substrs[0], "print") || !strcmp(substrs[0], "\tprint")){
        char *print_exp = substituteArgs(10, line, scopes);
        sprintf(outline, "\tprint(%s)\n", print_exp);
        addFuncLine(func, outline);
    } else if (!strcmp(substrs[1], "<-")) {
        // adds identifier to active scope
        // writes the formatted line to `outline`
        assignment(substrs, scopes, line, outline);
        addFuncLine(func, outline); 
    } else if (substrs[0][0] == '#' || substrs[0][0] == '\n'){
        return;
    } else { // we must have a function call
    // there's probably a better way i could've done this
        char temp[IDSIZE]; 
        int siz = strlen(substrs[0]);
        int i = 0;
        for(; substrs[0][i] != '(' && substrs[0][i] != '\n'; i++) {
            if (i + 1 == siz) {
                // throw a tantrum - invalid functioncall
            } else if (i > IDSIZE) {
                // function doesn't exist - we would've complained earlier
            } // that's a problem, we're writing the whole thing into it 
            temp[i] = substrs[0][i];
        }
        char *func_args = substituteArgs(i, line, scopes); // include left bracket - hence
        temp[i] = '\0';
        if (idInScope(temp, scopes)) {
            sprintf(outline, "%s %s;\n", temp, func_args); 
            // add it to the body
            addFuncLine(func, line);
        }
    }
}

char * handleGlobStat(char *substrs[], char line[], struct scope_info *scopes, struct func_attrs *func, struct func_attrs *main_func) {
    if(line[0] == '\n' || line[0] == '#') return "\0";
    if(!strcmp(substrs[0], "function")) {
        scopes->in_func = true;
        if(func->body != NULL) {
            // also need to allocate for header and free header...?
            free(func->body);
        } 
        *func = def_func; // initialises `func` to a new default
        constructFuncHeader(substrs, scopes, func);
    } else if (!strcmp(substrs[1], "<-")) {
        char *outline = malloc(BUFSIZE);
        assignment(substrs, scopes, line, outline);
        return outline; // to be printed directly to global scope
        // addFuncLine(main_func, outline);
    } else if(!strcmp(substrs[0], "print")) {
        char *outline = malloc(BUFSIZE);
        char *print_exp = substituteArgs(6, line, scopes);
        sprintf(outline, "print(%s)\n", print_exp);
        addFuncLine(main_func, outline);
    } else if (line[0] == ' ' || line[0] == '\t') {
        fprintf(stderr, "! Syntax error: lines outside function scope cannot start with ' ' or '\\t' \n");
        usage(true); 
    } else { // function call
        // should get "handleStat" to deal with the print statement too
        handleStat(substrs, scopes, line, main_func);
    }
    return "\0";
}

struct scope_info* initScopes() {
    struct scope_ids glo_scope;
    struct scope_ids func_scope;
    glo_scope.id_count = 0; func_scope.id_count = 0;
    struct scope_info *scopes = malloc(sizeof(struct scope_info));
    struct scope_info defau = {glo_scope, func_scope, false};
    *scopes = defau;
    return scopes;
}

void configureMain(struct func_attrs *main_func) {
    main_func->type = "int";
    main_func->header = "main(){\n";
}

FILE * constructC(char * argv[], int argc) {
    // i need to allocate memory for 
    struct scope_info* scopes = initScopes();
    // temporary c file to be written to and executed
    FILE *c_fp = fopen(TEMPNAME, "w"); // to read and write: "w+"
    // the .ml file provided by the path commandline argument
    FILE *ml_fp = fopen(argv[1], "r");
    if (ml_fp == NULL) {
        fprintf(stderr, "! Invalid file path - this file doesn't exist\n");
        closeFiles(c_fp, ml_fp);
    }
    // Validates and adds cmd arguments to `scopes->glo_scope.ids` 
    validateArgv(argc, argv, scopes, c_fp);
    // adds required preprocessor directives, such as headerfiles and definitions
    addPreDirectives(argv, c_fp, argc); 
    char line[BUFSIZE]; // to store the line read from the .ml file

    // main func will store lines required in the main function
    // e.g. globally scoped print statements, function calls, etc
    struct func_attrs main_func = def_func;
    configureMain(&main_func);
    // `func` simply holds the contents of a function while it is constructed
    // and is overwritten when a new function is encountered
    struct func_attrs func = def_func;
    // main loop
    char *substrs[BUFSIZE/12];  // holds substrings of a given line
    while(fgets(line, sizeof line, ml_fp) != NULL) {
        // case handling
        if (line[0] == '\n' || line[0] == '#') {
            continue;
        }
        splitByDelim(line, substrs, " "); 
        if (!scopes->in_func) { // in global scope
            // 
            char *globStat = handleGlobStat(substrs, line, scopes, &func, &main_func);
            if (!strcmp(globStat, "\0")) { // we have a function declaration 
                continue;
            } else {
                if (fputs(globStat, c_fp) == EOF) { 
                    // writes the globally scoped statement straight to the c file
                    fprintf(stderr, "@ Unexpected error writing to file %s \n", TEMPNAME);
                }
            }
        } else {
            // determine the line type in helper method
            // format in helper methods for each line type 
            // at the end of the if else chain, write the output string to func body
            if (!strcmp(substrs[0], "return")) { 
                // checks arguments and substitutes 0 for undeclared args
                // sets `ret_exp` to the expression to be returned
                char * ret_exp = substituteArgs(11, line, scopes);
                char func_line[BUFSIZE];
                sprintf(func_line, "\treturn %s;}\n", ret_exp);
                addFuncLine(&func, func_line);
                func.type = "float";
                writeFunc(&func, c_fp); 
                scopes->in_func = false;
                printf("FUNC IDENTIFIER: %s \n", func.identif);
                addToScope(scopes, func.identif); 
            } else if (line[0] != ' ' && line[0] != '\t') { // function body has finished - no return statement
                // make a function to add to a scope
                addFuncLine(&func, "}\n");
                writeFunc(&func, c_fp); // writes the function to TEMPNAME
                
                scopes->in_func = false;
                addToScope(scopes, func.identif);

                // handles the new statement outside of function scope
                handleStat(substrs, scopes, line, &main_func);
                // handleStat(main_func, func, substrs, scopes);
            } else {
                // handles print statements, assignment statements, and function calls.
                handleStat(substrs, scopes, line, &func);
            }
        }
    }
    // printf("%s \n", main_func.body[0]);
    writeFunc(&main_func, c_fp);
    if (fputs("}\n", c_fp) == EOF) { 
            // writes the globally scoped statement straight to the c file
        fprintf(stderr, "@ Unexpected error writing to file %s \n", TEMPNAME);
    }
    return c_fp;
}

int main(int argc, char * argv[]) {
    argc = 2;
    char *argv2[] = {"runml", "./samples/sample5.ml"};
    argv = argv2;
    // Validates commandline arguments, and passes commandline arguments to 
    // additional functions which construct and execute the required C file. 

    // Argument validation
    // prints usage msg via stderr stream
    if (argc < 2) usage(true); 
    //prints usage msg text via stdout stream if --help is called
    if (!strcmp("--help", argv[1])) usage(false); 

    // constructs a temporary C program `c_temp` from a valid ml file
    FILE * c_fp = constructC(argv, argc);
    fclose(c_fp);
    
    // // compiles `c_path`, executes the file constructed, and then deletes 
    // // `c_path` and the output file.
    // compileAndExec(c_path);
    return 0;
}
// passes 1, 2, 3, 4, segfaults on 5 for some reason...
// Not segfaulting on 5, but getting weird values in print statements + no terminating semi-colon
// really unsure why 5 isn't working
// Probably because it might be forgetting the arguments in the function header
// check up on how you're doing that
