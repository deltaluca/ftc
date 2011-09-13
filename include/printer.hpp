#pragma once

#include <iosfwd>
#include <vector>
#include <string>
#include <rose.h>

std::ostream& operator<<(std::ostream&, const std::vector<std::string>&);

class AstPrinter : public AstPrePostProcessing {
    int tabc;
    void tab() const;
    
public:
    AstPrinter();
    void preOrderVisit(SgNode* n);
    void postOrderVisit(SgNode* n);
};

