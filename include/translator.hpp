#pragma once
#include "rose.h"
#include <vector>
#include <map>
#include <string>

namespace ftc {

    class GFinder : public AstSimpleProcessing {
        SgGlobal* ret;
    public:
        void visit(SgNode* n);
        SgGlobal* find(SgNode* root);
    };

    SgFile* xf_file(SgFile*, SgProject**,
    
        std::map<std::string,SgFile*>&,
        std::vector<std::pair<SgGlobal*,std::string>>&
    );
    
    SgFile* xf_gen_header(SgFile*, SgProject*);
}
