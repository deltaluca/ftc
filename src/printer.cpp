#include "printer.hpp"

#include <iostream>

using std::cout;
using std::endl;
using std::vector;
using std::string;
using std::ostream;

AstPrinter::AstPrinter():tabc(1) {}

void AstPrinter::tab() const {
    for(int i = 0; i<tabc; i++) cout << ".. ";
}

void AstPrinter::preOrderVisit(SgNode* n) {
    tab();
    cout << n->sage_class_name() << endl;
    tabc++;
}
void AstPrinter::postOrderVisit(SgNode* n) {
    tabc--;
}

ostream& operator<<(ostream& out, const vector<string>& vec) {
    out << "[";
    for(auto i = vec.begin(); i!=vec.end(); i++) {
        if(i!=vec.begin()) out << ", ";
        out << "\"" << *i << "\"";
    }
    return out << "]";
}
