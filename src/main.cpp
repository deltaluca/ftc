#include <iostream>
#include <vector>
#include <rose.h>
#include "printer.hpp"
#include "translator.hpp"
#include <map>
#include <string>

using std::cout;
using std::endl;
using std::vector;
using std::string;
using std::map;
using std::pair;

using namespace SageBuilder;
using namespace SageInterface;

/*
    IMPROVEMENT:
    
    # (ftc performance)
    
        process fortran function declarations first to gather intents.
        
        I could keep the current method, but ensure that results are re-used rather than re-
        computed, but it would be more performant and simpler to do it the first way as the entire
        AST is traversed each time to discover the location of the FORTRAN declaration. Which seems
        rather wasteful than doing it in one traversal initially whether every intents list is
        needed or not in general.

    # (ftc source code)
    
        make xf_expr a pure function, return a struct containing the translated C expression
        together with a list of which variables would need to be additionality declared to
        execute the expression correctly.
        
        right now the impurity of the function in this regard is not very nice.
        
    # (ftc performance)
    
        go through the rose tutorial to see how to cancel a traversal of the AST for when I perform
        such traversals that only need traverse so far until results are gathered.
        
        Equally, determine/ask to find out if there is a way to shortcut the traversal into
        skipping a sub-tree. This is performed each time a scope is encountered using the sync node
        on postOrderVisit. But i don't much like this and would prefer skip_sub_tree(SgNode*) or
        something of the sort to exist somewhere in the ROSE library.
        
    # (translated code; aesthetics)
    
        util. to determine if an expression is pure or not. This would help neaten up function calls
        so that when some arguments require temporary variables to be created, I could determine
        which of the remaining arguments can be left where they are by analysing if their result
        would remain the same whether in the correct order, of after following arguments.
        
        eg:
        
        call test(x+1,y+1,z+1)
        
        if arg 2 is not intent(in), require temporary variable; and to maintain order of execution
        in the face of side effects require to do:
        
        auto arg_0 = x+1;
        auto arg_1 = y+1;
        auto arg_2 = z+1; //not strictly necessary for this arg., another improvement here!
        test(arg_0,&arg_1,arg_2);
        
        if however, could determine that the value of x+1 (z+1 is not important) is not changed by
        computing y+1, could rearrange this as:
        
        auto arg_1 = y+1;
        test(x+1,&arg_1,z+1);
        
        and maintain the same computational result.
        
        This would however most likely only be an aesthetic improvement, I would imagine gcc would
        perform such optimisation itself if the argments are not sufficiently complex.
        
        This is also a much more complex improvement, so likely just simply not worth the time/
        effort to implement, debug, test.

*/


int fortran_to_cpp(vector<string> argv);

int main(int _argc, char* _argv[]) {
    #if DEBUG
        cout << ">> ftc::main " << _argc << (vector<string>(_argv,_argv+_argc)) << endl;
    #endif
   
    vector<string> argv (_argv,_argv+_argc);
    if(argv.size()==1) {
        cout << "nothing to be done." << endl;
        return 0;
    }
    
    int errc = fortran_to_cpp(argv);
    if(errc!=0) return errc;
    
    return 0;
}

int fortran_to_cpp(vector<string> argv) {
    #if DEBUG
        cout << ">> fortran_to_cpp " << argv << endl;
    #endif

    SgProject* proj = frontend(argv);
    if(proj==NULL) {
        cout << "Cat. Error: Couldn't create SgProject" << endl;
        return 1;
    }
    
    SgProject* nproj = NULL;
    int errc = 0;
    
    //maps module name to the file in which it was ACTUALLY defined.
    //as Rose :: SgUseStatement->get_module() refers to a rose generated fortran file
    //rather than this original file we need. YAY ROSE WE LOVE YOU.
    map<string, SgFile*> module_map;
    vector<SgFile*> c_files;
    
    //map each file, to the module names it uses.
    vector<pair<SgGlobal*,string>> use_statements;
    
    for(int i = 0; i<proj->numberOfFiles(); i++) {
        SgFile& file = proj->get_file(i);
        
        //determine if this is actually a file we specified.
        //not a rose generated file for modules.
        //
        //rose likes to give us a FULL file path... but it seems that
        //SgFile::get_originalCommandLineArgumentList(); returns a subset of 'argv'
        //with cmdl[0] == argv[0]. So we use this as an ugly hack!
        auto cmdl = file.get_originalCommandLineArgumentList();
        bool found = (cmdl[0].compare(argv[0])==0);
        
        #if DEBUG
            cout << endl << endl << endl;
        #endif
       
        if(!found) {
            #if DEBUG
                cout << ".. ignoring ROSE generated file! :: " << file.getFileName() << endl;
            #endif
            continue;
        }
        
        //--------------------------------
        
        #if DEBUG
            cout << ".. process \"" << file.getFileName() << "\"" << endl;
            AstPrinter printer;
            printer.traverse(&file);
        #endif
        
        SgFile* nfile = NULL;
        try {
            nfile = ftc::xf_file(&file, &nproj,  module_map,use_statements);
        }catch(std::string err) {
            cout << "ERROR: " << err << endl;
            errc = 1;
        }
        
        #if DEBUG
            if(nfile!=NULL) {
                cout << ".. translated \"" << nfile->get_unparse_output_filename() << "\"" << endl;
                printer.traverse(nfile);
            }else
                cout << ".. translation... failed!" << endl;
        #endif
        
        if(nfile!=NULL) c_files.push_back(nfile);
    }
    
    #if DEBUG
        cout << endl << endl << endl;
        for(auto i = module_map.begin(); i != module_map.end(); i++) {
            cout << "Module (" << (*i).first << ") defined by: "
                 << (*i).second->get_unparse_output_filename() << endl;
        }
    #endif
    
    //add include directives
    for(auto i = use_statements.begin(); i!=use_statements.end(); i++) {
        SgGlobal* c_gscope = (*i).first;
        string  iname = (*i).second;
        assert(module_map.find(iname)!=module_map.end());
        
        SgFile* m_file = module_map[iname];
        string name = m_file->get_unparse_output_filename();
        name.erase(name.find_last_of('.'));
        name.append(".h");
        
        insertHeader(name.substr(name.find_last_of('/')+1), PreprocessingInfo::after, true, c_gscope);
        #if DEBUG
            cout << getEnclosingFileNode(c_gscope)->get_unparse_output_filename()
                 << " includes " << name << endl;
        #endif
    }

    #if DEBUG
        cout << endl << "C translation complete!!!!" << endl;
        cout << "...now to generate header accompaniments >:(" << endl;
    #endif
    
    //generate header accompaniments
    for(auto i = c_files.begin(); i!=c_files.end(); i++) {
        #if DEBUG
            cout << endl << endl << endl;
        #endif
    
        SgFile* hfile = NULL;
        try {
            hfile = ftc::xf_gen_header(*i, nproj);
        }catch(std::string err) {
            cout << "ERROR: " << err << endl;
            errc = 1;
        }
        
        #if DEBUG
            if(hfile!=NULL) {
                cout << ".. generated \"" << hfile->get_unparse_output_filename() << "\"" << endl;
                AstPrinter printer;
                printer.traverse(hfile);
            }else
                cout << ".. generation... failed!" << endl;
        #endif
        
        ftc::GFinder gfinder;
        
        SgGlobal* gscope = gfinder.find(*i);
        assert(gscope!=NULL);
        
        string path = hfile->get_unparse_output_filename();
        insertHeader(path.substr(path.find_last_of('/')+1), PreprocessingInfo::after, true, gscope);
    }
    
    if(nproj!=NULL)    
        nproj->unparse();
    
    #if DEBUG
        cout << endl << "..ACTUALLY. finished now!! byebye." << endl << endl;
    #endif

    return errc;
}


