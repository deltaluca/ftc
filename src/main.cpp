#include <iostream>
#include <vector>
#include <rose.h>
#include "printer.hpp"
#include "translator.hpp"
#include "main.hpp"
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

bool is_input_file(const SgFile& file, const vector<string>& argv) {
    //determine if this is actually a file we specified.
    //not a rose generated file for modules.
    //
    //rose likes to give us a FULL file path... but it seems that
    //SgFile::get_originalCommandLineArgumentList(); returns a subset of 'argv'
    //with cmdl[0] == argv[0]. So we use this as an ugly hack!
    auto cmdl = file.get_originalCommandLineArgumentList();
    bool found = (cmdl[0].compare(argv[0])==0);
     
    if(!found) {
        #if DEBUG
            cout << ".. ignoring ROSE generated file! :: " << file.getFileName() << endl;
        #endif
    }
    return found;    
}

//bugfix 13sep
vector<SgGlobal*> input_globals;
//
 
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

    //bugfix: 13sep: xf_get_fn_decl in translator.cpp cannot find
    //               other fortran files given in arguments
    //               so let's find all of them first so the translator
    //               can have access to all the input files.
    for(int i = 0; i<proj->numberOfFiles(); i++) {
        SgFile& file = proj->get_file(i);
        if(!is_input_file(file,argv)) continue;

	ftc::GFinder finder;
	SgGlobal* gscope = finder.find(&file);
        assert(gscope!=NULL);

	input_globals.push_back(gscope);
    }
   
    for(int i = 0; i<proj->numberOfFiles(); i++) {
        SgFile& file = proj->get_file(i);
        if(!is_input_file(file,argv)) continue;
        
        #if DEBUG
            cout << endl << endl << endl;
        #endif
       
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


