#include "translator.hpp"
#include "rose.h"
#include <iostream>
#include <vector>
#include <string>
#include <stack>
#include <assert.h>
#include <sstream>
#include <map>
#include <cstdlib>
#include <set>

using std::cout;
using std::endl;
using std::vector;
using std::string;
using std::stack;
using std::stringstream;
using std::map;
using std::set;
using std::pair;

using namespace SageBuilder;
using namespace SageInterface;

string getFTCVar()
{
    char* val = getenv("FTC");
    if(val==NULL) throw "Error: Enviromnent variable FTC does not exist";
    return string(val)+"/";
}

/*
    YES. this is ugly >.> the only global state in my translator.
    but, at least for now/until it becomes a problem is a lot less work
    then passing the state around.
*/

//since there is no way in rose to query existing header files included
//store them here instead.
set<pair<string,bool>> includes;

///_________________________________________________________________________________________________
///

/*

  Short-circuited visitor to search for the Intent attributes of a given set of arguments.

  Traversal should begin at function body.
*/

enum Intent {
    iIN, iOUT, iINOUT, iDEFAULT
};

class IntentVisitor : public AstSimpleProcessing {
public:
    vector<SgInitializedName*>* arguments;
    int cnt;
        
    map<string, Intent> result;
    
    IntentVisitor(vector<SgInitializedName*>* arguments, int cnt) {
        this->arguments = arguments;
        this->cnt = cnt;
    }
     
    void visit(SgNode* n) {
        #if DEBUG
            cout << "IntentVisitor visit " << n->sage_class_name() << endl;
        #endif
         
        if(isSgVariableDeclaration(n)) {
            auto* decl = isSgVariableDeclaration(n);
            
            auto tmod = decl->get_declarationModifier().get_typeModifier();
            auto intent = iDEFAULT;
            if     (tmod.isIntent_in   ()) intent = iIN;
            else if(tmod.isIntent_out  ()) intent = iOUT;
            else if(tmod.isIntent_inout()) intent = iINOUT;
        
            auto vars = decl->get_variables();
            for(auto i = vars.begin(); i != vars.end(); i++) {
                auto* init_name = *i;
                SgName var_name = init_name->get_name();
                bool found = false;
                    
                for(auto j = arguments->begin(); j != arguments->end(); j++) {
                    if(var_name.getString().compare((*j)->get_name().getString())==0) {
                        found = true;
                        break;
                    }
                 }
                    
                if(found) {
                    #if DEBUG
                        const char* ints[] = {"in","out","inout","default"};
                        cout << "IntentVisitor add " << var_name.getString() << " " << ints[intent] << endl;
                    #endif
                         
                    //this is a 'bit' of a hack. there's no need to reference/dereference arrays.
                    //just looks silly, so coerce it to an intent IN type so that it doesn't use
                    //pointers :)                   
                    if(isSgArrayType(init_name->get_type()))
                         result[var_name.getString()] = iIN;
                    else result[var_name.getString()] = intent;
                        
                    if((--cnt)==0) {
                        throw 0; //exit AST traversal.
                        return;
                    }
                }
            }
        }
    }
};

map<string, Intent> xf_fn_decl_intents(SgFunctionDeclaration* decl) {
    #if DEBUG
        cout << "xf_fn_decl_intents(.)" << endl;
    #endif
    IntentVisitor ivisitor (&decl->get_args(), decl->get_args().size());
    try {
        ivisitor.traverse(decl->get_definition()->get_body(),preorder);
    }catch(...) {
    }
    return ivisitor.result;
}

/**
    post: order of intents matches original function declaration order of argments.
        NOT the order in which the argument types/intents were defined.

	aka.

	subroutine fun(x,y)
	    integer :: y
	    integer :: x
	end subroutine fun

	xf_fn_decl_ordered_intents([fun_decl]) -> [ [Intent of x], [Intent of y] ]
**/
vector<Intent> xf_fn_decl_ordered_intents(SgFunctionDeclaration* decl) {
    #if DEBUG
        cout << "xf_fn_decl_ordered_intents(.)" << endl;
    #endif
    auto intents = xf_fn_decl_intents(decl);
    vector<Intent> ret;
    
    //iterate original arguments to build ordered list.
    //this enforces the post condition.
    auto& args = decl->get_args();
    for(auto i = args.begin(); i!=args.end(); i++) {
        string arg_name = (*i)->get_name().getString();
        assert(intents.find(arg_name)!=intents.end());
        
        ret.push_back(intents[arg_name]);
    }
    
    return ret;
}

///_________________________________________________________________________________________________
///

/**

   Transform FORTRAN type to C type.

**/

//stored along side array type so that indices can be calculated.
//also stored (sometimes) with pointer types, when the point is actually being used as an array
//and so we must handle that, and use this to calculate indices also.
//
//EDIT: rose likes to reuse types... so this is now stored in the variable declaration.
class ArrDimAttribute : public AstAttribute {
public:
    //array of dimension N:
    //
    // |lbounds| = N
    // |sizes| = N-1
    //
    // arr(i,j,k) -> arr[(k-lbounds[0])*sizes[0] + (j-lbounds[1])*sizes[1] + (i-lbounds[2])]
    //
    vector<SgExpression*> lbounds;
    vector<SgExpression*> sizes;
    
    ArrDimAttribute() {}
};


namespace ftc {
    SgType* xf_type(SgType* type, bool argpar=false, ArrDimAttribute** arr_attr=NULL);
    SgExpression* xf_expr(SgExpression* expr, vector<Intent>* intents=NULL);
}

SgType* ftc::xf_type(SgType* type, bool argpar, ArrDimAttribute** arr_attr) {
    assert(type!=NULL);
    
    #if DEBUG
        cout << "xf_type(.)" << endl;
    #endif

    //handle arrays
    if(isSgArrayType(type)) {
        #if DEBUG
            cout << ".. arrtype" << endl;
        #endif
        auto* arrtype = isSgArrayType(type);
        auto* base = xf_type(arrtype->get_base_type());
        
        //gather size information.
        auto diminfo = arrtype->get_dim_info()->get_expressions();
        SgExpression* size = NULL;
        auto* dimattr = new ArrDimAttribute;
        
        for(auto i = diminfo.begin(); i!=diminfo.end(); i++) {
            auto* dim = *i;
            SgExpression* subsize = NULL;
            SgExpression* lbound = NULL;
            if(isSgSubscriptExpression(dim)) {
                auto* shape = isSgSubscriptExpression(dim);
                lbound = xf_expr(shape->get_lowerBound());
                auto* ubound = shape->get_upperBound();
                if(!isSgAsteriskShapeExp(ubound)) {
                    auto* ubound = xf_expr(shape->get_upperBound());
                    if(isSgIntVal(lbound) && isSgIntVal(ubound))
                        subsize = buildIntVal(isSgIntVal(ubound)->get_value()-isSgIntVal(lbound)->get_value()+1);
                    else if(!isSgIntVal(lbound) || isSgIntVal(lbound)->get_value()!=0)
                        subsize = buildAddOp(buildIntVal(1),buildSubtractOp(ubound,lbound));
                    else
                        subsize = buildAddOp(buildIntVal(1),ubound);
                }
            }else if(!isSgAsteriskShapeExp(dim)) {
                lbound = buildIntVal(1);
                subsize = dim;
            }else
                lbound = buildIntVal(1);
            
            if(size!=NULL) {
                dimattr->sizes.push_back(size);
                if(subsize!=NULL) {
                    if(isSgIntVal(size) && isSgIntVal(subsize))
                        size = buildIntVal(isSgIntVal(size)->get_value() * isSgIntVal(subsize)->get_value());
                    else
                        size = buildMultiplyOp(size, subsize);
                }
            }else if(subsize!=NULL)
                size = subsize;
                    
            dimattr->lbounds.push_back(lbound); 
        }
        
        SgType* ret = NULL;
        if(!argpar)
             ret = buildArrayType(base, size);
        else ret = buildPointerType(base);
            
        /*if(ret->attributeExists("dim")) {
            auto* odimattr = static_cast<ArrDimAttribute*>(ret->getAttribute("dim"));
            cout << odimattr->lbounds.size() << endl;
            cout << dimattr->lbounds.size() << endl;
        }
        ret->addNewAttribute("dim", dimattr);*/
        if(arr_attr!=NULL) *arr_attr = dimattr;
        
        return ret;
        
        /*auto* arrtype = isSgArrayType(type);
        auto* base = xf_type(arrtype->get_base_type());
        
        auto diminfo = arrtype->get_dim_info()->get_expressions();
        for(auto i = diminfo.begin(); i!=diminfo.end(); i++) {
            AstAttribute* dimattr = NULL;
            auto* dim = *i;
            if(isSgSubscriptExpression(dim)) {
                auto* shape = isSgSubscriptExpression(dim);
                auto* lbound = xf_expr(shape->get_lowerBound());
                if(!argpar) {
                    auto* ubound = shape->get_upperBound();
                    if(!isSgAsteriskShapeExp(ubound)) {
                        ubound = xf_expr(ubound);
                        if(isSgIntVal(lbound) && isSgIntVal(ubound)) {
                            dim = buildIntVal(
                                1 + (isSgIntVal(ubound)->get_value() - isSgIntVal(lbound)->get_value())
                            );
                        }else
                            dim = buildAddOp(buildIntVal(1),buildSubtractOp(ubound,lbound));                    
                    }else
                        dim = NULL;
                }else
                    dim = NULL;
                    
                dimattr = new ArrDimAttribute(lbound);
            }else {
                if(!argpar) {
                    if(!isSgAsteriskShapeExp(dim)) 
                         dim = xf_expr(dim);
                    else dim = NULL;
                }else
                    dim = NULL;
                dimattr = new ArrDimAttribute(buildIntVal(1));
            }

            auto* ret = buildArrayType(base, dim);
            ret->addNewAttribute("dim",dimattr);
            base = ret;
        }
        
        return base;*/
    }
    
    if(isSgTypeVoid(type)) return buildVoidType();
    if(isSgTypeBool(type)) return buildIntType(); //NOTE bool->int
    if(isSgTypeChar(type)) return buildCharType();
        
    if(isSgTypeInt(type)) {
        auto* itype = isSgTypeInt(type);
        auto* kind = itype->get_type_kind();
        if(kind!=NULL) {
            if(isSgIntVal(kind)) {
                int value = isSgIntVal(kind)->get_value();
                switch(value) {
                    case 1: return buildCharType();
                    case 2: return buildShortType();
                    case 4: return buildIntType  ();
                    case 8: return buildLongType ();
                }
            }else throw (std::string)"Integer type with non integral kind?";
        }else return buildIntType();
    }
    if(isSgTypeFloat(type)) {
        auto* ftype = isSgTypeFloat(type);
        auto* kind = ftype->get_type_kind();
        if(kind!=NULL) {
            if(isSgIntVal(kind)) {
                int value = isSgIntVal(kind)->get_value();
                switch(value) {
                    case  4: return buildFloatType();
                    case  8: return buildDoubleType();
                }
            }else throw (std::string)"Floating type with non integral kind?";
        }else return buildFloatType();
    }
    
    if(isSgTypeString(type)) {
        //auto* str = isSgTypeString(type);
        //auto* length = str->get_lengthExpression();
        return buildStringType(buildNullExpression());
    }
    
    cout << type->sage_class_name() << endl;
    throw (std::string)"Unhandled SgType in tcf::xf_type :: SgType* -> SgType*";
    return NULL;
}

///_________________________________________________________________________________________________
///

namespace ftc {
    
    SgValueExp* xf_value_exp(SgValueExp* val) {
        assert(val!=NULL);
        
        #if DEBUG
            cout << "xf_value_exp(.)" << endl;
        #endif
        
        if(isSgBoolValExp(val))
            return buildIntVal(isSgBoolValExp(val)->get_value()); //NOTE bool->int
        
        #define DEF(T) else if(isSg##T##Val(val)) \
            return build##T##Val(isSg##T##Val(val)->get_value());
        
        DEF(Int)
        DEF(Short)
        DEF(LongInt)
        DEF(LongLongInt)
        
        DEF(Float)
        DEF(Double)
        DEF(LongDouble)
        DEF(String)
        
        #undef DEF
    
        cout << val->sage_class_name() << endl;
        throw (std::string)"Unhandled Value type in tcf::xf_expr::xf_value_exp :: SgValueExp* -> SgValueExp*";
        return NULL;
    }
    
    ///_____________________________________________________________________________________________
    ///

    SgExpression* xf_unop_exp(SgUnaryOp* unop) {
        assert(unop!=NULL);
        
        #if DEBUG
          cout << "xf_unop_exp(.)" << endl;
        #endif
        
        auto* nexp = ftc::xf_expr(unop->get_operand());
        
        SgExpression* ret = NULL;
        
        #define DEF(T) else if(isSg##T##Op(unop)) \
            ret = build##T##Op(nexp);
        
        if(false) {}
        DEF(MinusMinus)
        DEF(PlusPlus)
        DEF(Not)
        DEF(BitComplement)
        
        #undef DEF
        
        else if(isSgUnaryAddOp(unop)) ret = nexp;
        else if(isSgMinusOp(unop)) {
            if     (isSgIntVal(nexp))   ret = buildIntVal(-isSgIntVal(nexp)->get_value());
            else if(isSgFloatVal(nexp)) ret = buildFloatVal(-isSgFloatVal(nexp)->get_value());
            else ret = buildMinusOp(nexp);
        }
        
        if(ret==NULL) {
            cout << unop->sage_class_name() << endl;
            throw (std::string)"Unhandled UnaryOp type in tcf::xf_expr::xf_unop_exp :: SgUnaryOp* -> SgUnaryOp*";
        } else {
            //set postfix/prefix mode.
            if(isSgUnaryOp(ret))
                isSgUnaryOp(ret)->set_mode(unop->get_mode());
            return ret;
        }
    }
    
    ///_____________________________________________________________________________________________
    ///
    
    SgExpression* xf_binop_exp(SgBinaryOp* binop) {
        assert(binop!=NULL);
        
        #if DEBUG
            cout << "xf_binop_exp(.)" << endl;
        #endif
        
        auto* lexp = ftc::xf_expr(binop->get_lhs_operand());
        auto* rexp = ftc::xf_expr(binop->get_rhs_operand());
        
        SgBinaryOp* ret = NULL;
        
        #define DEF(T) else if(isSg##T##Op(binop)) \
            ret = build##T##Op(lexp,rexp);
            
        if(false) {}
        DEF(Add)
        DEF(AndAssign)
        DEF(And)
        DEF(Assign)
        DEF(BitAnd)
        DEF(BitOr)
        DEF(BitXor)
        DEF(DivAssign)
        DEF(Divide)
        DEF(Equality)
        DEF(GreaterOrEqual)
        DEF(GreaterThan)
        DEF(LessOrEqual)
        DEF(LessThan)
        DEF(MinusAssign)
        DEF(MultAssign)
        DEF(Or)
        DEF(Multiply)
        DEF(NotEqual)
        DEF(PlusAssign)
        DEF(Subtract)
        DEF(XorAssign)
   
        else if(isSgExponentiationOp(binop)) {
            SgType* type = binop->get_type();
            
            #if DEBUG
                cout << ".. xf_binop_exp::exponentiation" << endl;
            #endif
            
            auto* fn_call = buildFunctionCallExp(SgName("pow"),buildDoubleType(),buildExprListExp(lexp,rexp));
            
            if(type->isIntegerType())
                 return buildCastExp(fn_call, buildIntType());
            else return fn_call;
        }
        
        else if(isSgPntrArrRefExp(binop)) {
            assert(isSgExprListExp(rexp));
            
            auto* arrtype = lexp->get_type();
            ArrDimAttribute* dimattr = NULL;
            
            assert(isSgVarRefExp(lexp));
            auto* lvar = isSgVarRefExp(lexp);
            auto* decl = lvar->get_symbol()->get_declaration();
            assert(decl->attributeExists("dim"));
            
            dimattr = static_cast<ArrDimAttribute*>(decl->getAttribute("dim"));
            
            auto indices = isSgExprListExp(rexp)->get_expressions();
            SgExpression* index = NULL;
            for(int i = 0; i<indices.size(); i++) {
                auto* ind = indices[i];
                auto* lbound = dimattr->lbounds[i];
                
                SgExpression* sub_index = NULL;
                if(isSgIntVal(ind) && isSgIntVal(lbound))
                    sub_index = buildIntVal(isSgIntVal(ind)->get_value() - isSgIntVal(lbound)->get_value());
                else if(!isSgIntVal(lbound) || isSgIntVal(lbound)->get_value()!=0)
                    sub_index = buildSubtractOp(ind,lbound);
                else
                    sub_index = ind;
                
                if(i!=0) {
                    auto* size = dimattr->sizes[i-1];
                    
                    if(isSgIntVal(size) && isSgIntVal(sub_index))
                        sub_index = buildIntVal(isSgIntVal(sub_index)->get_value() * isSgIntVal(size)->get_value());
                    else if(!isSgIntVal(size) || isSgIntVal(size)->get_value()!=1)
                        sub_index = buildMultiplyOp(sub_index, size);
                }
                
                if(index==NULL) index = sub_index;
                else if(isSgIntVal(index) && isSgIntVal(sub_index))
                    index = buildIntVal(isSgIntVal(index)->get_value() + isSgIntVal(sub_index)->get_value());
                else if(isSgIntVal(index) && isSgIntVal(index)->get_value()==0)
                    index = sub_index;
                else if(!isSgIntVal(sub_index) || isSgIntVal(sub_index)->get_value()!=0)
                    index = buildAddOp(index,sub_index);
            }
            
            return buildPntrArrRefExp(lexp, index);
        }
        
        /*else if(isSgPntrArrRefExp(binop)) {
            assert(isSgExprListExp(rexp));

            auto indices = isSgExprListExp(rexp)->get_expressions();
            auto* lhs = lexp;
            for(int i = indices.size()-1; i>=0; i--) {
                auto* ind = indices[i];
                
                auto* arrtype = lhs->get_type();
                auto* dimattr = static_cast<ArrDimAttribute*>(arrtype->getAttribute("dim"));
                auto* lbound = dimattr->lbound;
                
                SgExpression* rhs = NULL;
                if(isSgIntVal(ind) && isSgIntVal(lbound))
                    rhs = buildIntVal(isSgIntVal(ind)->get_value() - isSgIntVal(lbound)->get_value());
                else if(!isSgIntVal(lbound) || isSgIntVal(lbound)->get_value()!=0)
                    rhs = buildSubtractOp(ind, lbound);
                else
                    rhs = ind;
                    
                lhs = buildPntrArrRefExp(lhs, rhs);
            }
            
            return lhs;
        }*/
            
        #undef DEF
        
        if(ret==NULL) {
            cout << binop->sage_class_name() << endl;
            throw (std::string)"Unhandled BinaryOp type in tcf::xf_expr::xf_binop_exp :: SgBinaryOp* -> SgBinaryOp*";
        } else
            return ret;
    }
}

///_________________________________________________________________________________________________
///

namespace ftc {
    class FFinder : public AstSimpleProcessing {
        string name;
    public:
        SgFunctionDeclaration* result;
    
        FFinder(const string& name) {
            this->name = name;
            result = NULL;
        }
        
        void visit(SgNode* n) {
            if(isSgFunctionDeclaration(n)) {
                auto* decl = isSgFunctionDeclaration(n);
                if(decl->get_name().getString().compare(name)==0) {
                    result = isSgFunctionDeclaration(n);
                    throw 0; //exit AST traversal.
                }
            }
        }
    };

    SgFunctionDeclaration* xf_get_fn_decl(const string& name, SgNode* root) {
        FFinder finder (name);
        try {
            finder.traverse(root, preorder);
        }catch(...) {
        }
        return finder.result;
    }
    
    //used (FOR THE MOMENT!) to generate unique FOR loop variables.
    //and now, perhaps slightly confusingly; also for argument temp. variables
    int FOR_VARIABLE_NAME = 0;
    string genForVariableName(const std::string& pre) {
        stringstream ret;
        ret << pre;
        ret << FOR_VARIABLE_NAME;
        FOR_VARIABLE_NAME++;
        return ret.str();
    }
}

//intents field used for translating function call argument lists correctly.
//
//IMPORTANT: this is NOT a pure function. IN the case that additional var stores are required
//           for function calls, this method WILL inject those declarations to the current scope.
SgExpression* ftc::xf_expr(SgExpression* expr, vector<Intent>* intents) {
    assert(expr!=NULL);
    
    #if DEBUG
        cout << "xf_expr(.)" << endl;
    #endif
    
    if(isSgVarRefExp(expr)) {
        auto* ref = isSgVarRefExp(expr);
        #if DEBUG
            cout << "xf_expr::var_exp" << endl;
        #endif
        auto* sym = lookupVariableSymbolInParentScopes(ref->get_symbol()->get_name());
        if(sym==NULL) {
            #if DEBUG
                cout << " NO C-SYMBOL FOUND!! " << endl << " !! " << endl;
            #endif
            return buildVarRefExp(ref->get_symbol()->get_name());
        }else {
            auto* type = sym->get_type();
            auto* var = buildVarRefExp(sym->get_name());
            
            if(isSgPointerType(type)) {
                #if DEBUG
                    cout << ".. pointer type!" << endl;
                #endif
    
                //an array in disguise            
                if(var->get_symbol()->get_declaration()->attributeExists("dim"))
                    return var;
                else
                    return buildPointerDerefExp(var);
            }else
                return var;
        }
    }
    
    if(isSgValueExp(expr)) {
        auto* val = isSgValueExp(expr);
        return xf_value_exp(val);
    }
    
    if(isSgUnaryOp(expr)) {
        auto* unop = isSgUnaryOp(expr);
        return xf_unop_exp(unop);
    }
    
    if(isSgBinaryOp(expr)) {
        auto* binop = isSgBinaryOp(expr);
        return xf_binop_exp(binop);
    }

    //ASSUMPTION: Fortran doesn't have function pointers (i think). so apart from function calls
    //which are handled seperately below, this will only ever be for assignment/use of a function 
    //return value.
    if(isSgFunctionRefExp(expr)) {
        #if DEBUG
            cout << "xf_expr::fun_ref_exp (return value)" << endl;
        #endif
        return buildVarRefExp(SgName("__retv"));
    }
    
    //ASSUMPTION: Fortran doesn't have function pointers, nor arrays of functions (i think).
    //so things like fn[i]() is not possible, and the expression supplying the function to call
    //will only ever be an SgFunctionRefExp!
    if(isSgFunctionCallExp(expr)) {
        auto* fcall = isSgFunctionCallExp(expr);
        #if DEBUG
            cout << "xf_expr::fun_call_exp" << endl;
        #endif
        
        auto* fexp = fcall->get_function();
        assert(isSgFunctionRefExp(fexp));
        
        auto* fsym = isSgFunctionRefExp(fexp)->get_symbol();
        SgName fn_name = fsym->get_name();
        
        //find the FORTRAN function declaration. the C declaration may not exist yet!
        //
        //I tried doing this via lookupFunctionSymbolInParentScopes(getScope(expr))
        //but it doesn't work! so this will do...
        auto* fort_decl = xf_get_fn_decl(fn_name.getString(), getGlobalScope(expr));
        SgExprListExp* fn_args = NULL;
        if(fort_decl!=NULL) {
            auto intents = xf_fn_decl_ordered_intents(fort_decl);
            fn_args = isSgExprListExp(xf_expr(fcall->get_args(), &intents));
        }else {
            string fn = fn_name.getString();
            fn_args = isSgExprListExp(xf_expr(fcall->get_args()));
            
            if(fn.compare("abs")==0) {
                assert(fn_args->get_expressions().size()==1);
                auto* arg = fn_args->get_expressions().front();
                if(isSgTypeInt(arg->get_type())) {}
                else fn_name = "fabs"; //redirect.
            }
            
            if(fn.compare("int")==0 || fn.compare("logical")==0 || fn.compare("ichar")==0) {
                assert(fn_args->get_expressions().size()==1);
                auto* arg = fn_args->get_expressions().front();
                
                return buildCastExp(arg, buildIntType());
            }                
            
            if(fn.compare("real")==0) {
                assert(fn_args->get_expressions().size()==1);
                auto* arg = fn_args->get_expressions().front();
                
                return buildCastExp(arg, buildDoubleType());
            }
            
            if(fn.compare("get_environment_variable")==0) {
                auto args = fn_args->get_expressions();
                assert(args.size()==2);
                
                includes.insert(pair<string,bool>("stdlib.h",true));
                
                vector<SgExpression*> ret_args;
                ret_args.push_back(args[0]);
                return buildAssignOp(
                    args[1],
                    buildFunctionCallExp(
                        buildFunctionRefExp("getenv"),
                        buildExprListExp(ret_args)
                    )
                );
            }
        }
        return buildFunctionCallExp(
            buildFunctionRefExp(fn_name),
            isSgExprListExp(fn_args)
        );
    }
    
    if(isSgExprListExp(expr)) {
        auto exprs = isSgExprListExp(expr)->get_expressions();
        #if DEBUG
            cout << "xf_expr::expr_list" << endl;
        #endif
        vector<SgExpression*> ret_exprs;
        
        vector<SgExpression*> texprs;
        vector<bool> pointer_arg;
        bool any = false;
        //first determine if any arguments require a new variable for the function call
        //in this case, we should pre-calculate ALL arguments to keep order of execution
        //well defined in the face of possible side-effects.
        int ind = 0;
        for(auto i = exprs.begin(); i!=exprs.end(); i++) {
            auto* ex = xf_expr(*i);
            texprs.push_back(ex); //only want to call xf_expr on each expression once
            //as this is an unpure function and may generate variable declarations for temporaries.
            
            bool parg = false;
            if(intents!=NULL) {
                auto intent = (*intents)[ind++];
                bool ref = false;
                
                switch(intent) {
                    case iIN: break;
                    default: ref = true;
                }
                
                if(ref) {
                    if(isSgPointerDerefExp(ex)) {}
                    else {
                        if(isSgVarRefExp(ex)) {}
                        else {
                            any = true;
                            parg = true;
                        }
                    }
                }
            }
            
            pointer_arg.push_back(parg);
        }
        
        //compile function call.
        ind = 0;
        for(auto i = texprs.begin(); i!=texprs.end(); i++) {
            auto* ex = *i;
            
            if(any) {
                //need temporary variable
                string name = genForVariableName("__arg");
                SgType* type = ex->get_type();
                auto* decl = buildVariableDeclaration(
                    SgName(name), type,
                    buildAssignInitializer(
                        ex,
                        type
                    )
                );
                appendStatement(decl);
                
                if(pointer_arg[ind]) {
                    ret_exprs.push_back(
                        buildAddressOfOp(
                            buildVarRefExp(SgName(name))
                        )
                    );
                }else
                    ret_exprs.push_back(buildVarRefExp(SgName(name)));
                
            } else if(intents!=NULL) {
                auto intent = (*intents)[ind];
                bool ref = false;
                
                switch(intent) {
                    case iIN: break;
                    default: ref = true;
                }
                
                if(ref) {
                    if(isSgPointerDerefExp(ex)) {
                        auto* unop = isSgPointerDerefExp(ex);
                        ret_exprs.push_back(unop->get_operand());
                    }else
                        ret_exprs.push_back(buildAddressOfOp(ex));   
                }else ret_exprs.push_back(ex);
            }else
                ret_exprs.push_back(ex);
            
            ind++;
        }
   
        return buildExprListExp(ret_exprs);
    }

    cout << expr->sage_class_name() << endl;
    throw (std::string)"Unhandled SgExpression in tcf::xf_expr :: SgExpression* -> SgExpression*";
    return NULL;
}

///_________________________________________________________________________________________________
///

namespace ftc {

    void GFinder::visit(SgNode* n) {
        if(isSgGlobal(n)) {
            ret = isSgGlobal(n);
            throw 0;
        }
    }
    SgGlobal* GFinder::find(SgNode* root) {
        ret = NULL;
        try {
            traverse(root,preorder);
        }catch(...) {}
        return ret;
    }

    class XfVisitor : public AstPrePostProcessing {
        //used when I want to skip the traversal of an entire sub-tree.
        //keep ignoring nodes until postOrderVisit is called with the sync node.
        SgNode* sync;
        
    public:
        //used to ignore parameter declarations in procedure bodies
        vector<SgInitializedName*>* arguments;
    
        XfVisitor() {
            sync = NULL;
            arguments = NULL;
        }

        void preOrderVisit(SgNode* n);
        void postOrderVisit(SgNode* n);
    };
}

///_________________________________________________________________________________________________
///

namespace ftc {
    void xf_pushScopeStack(SgScopeStatement* n) {
        assert(n!=NULL);
        #if DEBUG
            cout << "pushScopeStack(.)" << endl;
        #endif
        pushScopeStack(n);
    }
    void xf_popScopeStack() {
        #if DEBUG
            cout << "popScopeStack()" << endl;
        #endif
        popScopeStack();
    }
    void xf_appendStatement(SgStatement* n) {
        assert(n!=NULL);
        #if DEBUG
            cout << "appendStatement(.)" << endl;
        #endif
        appendStatement(n);
    }
    void xf_appendArg(SgFunctionParameterList* list, SgInitializedName* n) {
        assert(list!=NULL);
        assert(n!=NULL);
        #if DEBUG
            cout << "appendArg(.,.)" << endl;
        #endif
        appendArg(list,n);
    }
}

///_________________________________________________________________________________________________
///

namespace ftc {

    void xf_block(SgBasicBlock* fort_block, SgBasicBlock* c_block, vector<SgInitializedName*>* arguments=NULL, bool lazy=false) {
        assert(fort_block!=NULL);
        assert(c_block!=NULL);

        #if DEBUG
            cout << "xf_block(.,.,.)" << endl;
        #endif

        if(!lazy)
            xf_pushScopeStack(c_block);
        
        //traverse block;
        XfVisitor visitor;
        
        //ignore any declaration of function arguments in the body.
        visitor.arguments = arguments;
        visitor.traverse(fort_block);
    
        if(!lazy)    
            xf_popScopeStack();
    }
    
    //----------------------------------------------------------------------------------------------
    
    void xf_fn_decl(SgProcedureHeaderStatement* decl) {
        assert(decl!=NULL);
        
        #if DEBUG
            cout << "xf_fn_decl(.)" << endl;
        #endif
        
        SgName fn_name = decl->get_name();
        #if DEBUG
            cout << ".. " << fn_name.getString() << endl;
        #endif
        
        auto intents = xf_fn_decl_intents(decl);
        
        auto* fn_args = buildFunctionParameterList();
        auto args = decl->get_args();
        for(auto i = args.begin(); i!=args.end(); i++) {
            auto* init_name = *i;
            SgName arg_name = init_name->get_name();
            
            ArrDimAttribute* arr_attr = NULL;
            SgType* arg_type = ftc::xf_type(init_name->get_type(), true, &arr_attr);
            
            assert(intents.find(arg_name.getString())!=intents.end());
            auto intent = intents[arg_name.getString()];

            switch(intent) {
                case iIN: break;
                default: //iOUT iINOUT iDEFAULT
                    arg_type = buildPointerType(arg_type);
            }
            
            auto* iname = buildInitializedName(arg_name, arg_type);
            if(arr_attr!=NULL)
                iname->addNewAttribute("dim", arr_attr);
            xf_appendArg(fn_args, iname);
            
            #if DEBUG
                cout << ".. arg " << arg_name.getString() << endl;
            #endif
        }
        
        SgType* fn_return =
            decl->isFunction()
              ? ftc::xf_type(decl->get_type()->get_return_type())
              : buildVoidType()
        ;
        
        auto* fn_decl = buildDefiningFunctionDeclaration(fn_name, fn_return, fn_args);
        auto* fn_defn = fn_decl->get_definition();
        auto* fn_body = fn_defn->get_body();
        
        xf_appendStatement(fn_decl);
        
        auto* body = decl->get_definition()->get_body();
        xf_pushScopeStack(fn_body);
        
        if(decl->isFunction()) {
            string fn_ret_name = "__retv";
            auto* ret_decl = buildVariableDeclaration(SgName(fn_ret_name), fn_return);
            xf_appendStatement(ret_decl);
        }
        
        xf_block(body, fn_body, &fn_decl->get_args(), true);
        
        xf_popScopeStack();
    }   
    
    //---------------------------------------------------------------------------------------------- 
    
    //arguments list so as to ignore declarations of those arguments in a procedure body.
    void xf_var_decl(SgVariableDeclaration* decl, vector<SgInitializedName*>* arguments) {
        assert(decl!=NULL);
        
        #if DEBUG
            cout << "xf_var_decl(.,.)" << endl;
        #endif
        
        auto vars = decl->get_variables();
        for(auto i = vars.begin(); i != vars.end(); i++) {
            auto* init_name = *i;
            SgName var_name = init_name->get_name();
            #if DEBUG
                cout << ".. " << var_name.getString() << endl;
            #endif
            
            //determine this variable is not a procedure argument to be ignored!
            if(arguments!=NULL) {
                bool found = false;
                
                for(auto j = arguments->begin(); j != arguments->end(); j++) {
                    if(var_name.getString().compare((*j)->get_name().getString())==0) {
                        found = true;
                        break;
                    }
                }
                
                if(found) {
                    #if DEBUG
                        cout << ".. .. ignored var decl. (argument)" << endl;
                    #endif
                    continue;
                }
            }
            
            auto* init = init_name->get_initializer();
            SgInitializer* var_init = NULL;
            if(init!=NULL) {
                assert(isSgAssignInitializer(init));
                var_init = buildAssignInitializer(xf_expr(isSgAssignInitializer(init)->get_operand()));
            }
            
            //onwards!
            ArrDimAttribute* arr_attr = NULL;
            SgType* var_type = ftc::xf_type(init_name->get_type(), false, &arr_attr);
            auto* var_decl = buildVariableDeclaration(var_name,var_type,var_init);
            if(arr_attr!=NULL)
                var_decl->get_variables()[0]->addNewAttribute("dim", arr_attr);
            xf_appendStatement(var_decl);
        }
    }
    
    //----------------------------------------------------------------------------------------------
    
    void xf_fortran_do(SgFortranDo* fdo) {
        assert(fdo!=NULL);
        
        #if DEBUG
            cout << "xf_fortran_do(.)" << endl;
        #endif
        
        auto* do_init = fdo->get_initialization();
        if(!isSgAssignOp(do_init)) {
            throw (std::string)"Unhandled FortranDo Initialisation expression in ftc::xf_fortran_do";
            return;
        }
        
        auto* for_init_exp = isSgAssignOp(ftc::xf_expr(do_init));
        auto* counter = for_init_exp->get_lhs_operand();
        if(!isSgVarRefExp(counter)) {
            throw (std::string)"Unhandled FortranDo Initialisation 2; assignOp LHS was not a variable";
            return;
        }
        auto* for_init = buildExprStatement(for_init_exp);
        xf_appendStatement(for_init);
        
        //
        
        auto* do_bound = fdo->get_bound();
        string bound_name = genForVariableName("__fbound");
        SgType* bound_type = ftc::xf_type(do_bound->get_type());
        auto* bound_exp = ftc::xf_expr(do_bound);
        auto* bound_decl = buildVariableDeclaration(
            SgName(bound_name), bound_type,
            buildAssignInitializer(bound_exp)
        );
        xf_appendStatement(bound_decl);
        
        auto* do_step = fdo->get_increment();
        SgStatement* for_test = NULL;
        
        if(isSgNullExpression(do_step)) {
            for_test = buildExprStatement(
                buildLessOrEqualOp(counter, buildVarRefExp(SgName(bound_name)))
            );
        }else {
            string limit_name = genForVariableName("__fdir");
            auto* limit_decl = buildVariableDeclaration(
                SgName(limit_name), buildIntType(),
                buildAssignInitializer(
                    buildLessOrEqualOp(counter,buildVarRefExp(SgName(bound_name)))
                )
            ); 
            xf_appendStatement(limit_decl);
                   
            for_test = buildExprStatement(
                buildConditionalExp(
                    buildVarRefExp(SgName(limit_name)),
                    buildLessOrEqualOp(counter, buildVarRefExp(SgName(bound_name))),
                    buildGreaterOrEqualOp(counter, buildVarRefExp(SgName(bound_name)))
                )
            );
        }

        //
        
        SgExpression* for_step = NULL;
        if(isSgNullExpression(do_step)) {
            for_step = buildPlusPlusOp(counter);
            #if DEBUG
                cout << ".. using default step (++)" << endl;
            #endif
        }else {
            string step_name = genForVariableName("__fstep");
            SgType* step_type = ftc::xf_type(do_bound->get_type());
            auto* step_exp = ftc::xf_expr(do_step);
            auto* step_decl = buildVariableDeclaration(
                SgName(step_name), step_type,
                buildAssignInitializer(step_exp)
            );
            xf_appendStatement(step_decl);
            
            for_step = buildPlusAssignOp(counter, buildVarRefExp(SgName(step_name)));
        }
        
        auto* body = buildBasicBlock();
        auto* forl = buildForStatement(buildNullStatement(),for_test,for_step,body);
        xf_appendStatement(forl);
                
        xf_block(fdo->get_body(), body, NULL);
    }
}

//--------------------------------------------------------------------------------------------------

namespace ftc {
    string xf_format(SgExpression* expr) {
        assert(expr!=NULL);
        SgType* type = expr->get_type();
        if     (isSgTypeString(type)) return (string)"%s";
        else if(isSgTypeInt   (type)) return (string)"%d";
        else if(isSgTypeFloat (type)) {
            auto* kind = type->get_type_kind();
            if(kind!=NULL && isSgIntVal(kind) && isSgIntVal(kind)->get_value()==8)
                 return (string)"%lf";
            else return (string)"%f";
        }
        else if(isSgTypeChar  (type)) return (string)"%c";
        
        cout << type->sage_class_name() << endl;
        throw (string)"Unhandled expression type in xf_format";
    }
}

//--------------------------------------------------------------------------------------------------

/*
    YES. this is ugly >.> the only global state in my translator.
    but, at least for now/until it becomes a problem is a lot less work
    then passing the state around.
*/

namespace ftc {
    map<string, SgFile*>* module_map;
    vector<pair<SgGlobal*,string>>* use_statements;
}

void ftc::XfVisitor::preOrderVisit(SgNode* n) {
    if(sync!=NULL) return;
    
    //ignore list.
    if(isSgGlobal(n)) return;
    if(isSgContainsStatement(n)) return;
    if(isSgClassDefinition(n)) return;
    if(isSgImplicitStatement(n)) return;
    if(isSgBasicBlock(n)) return;
       
    #if DEBUG     
        cout << "visit " << n->sage_class_name() << endl;
    #endif
    
    if(isSgModuleStatement(n)) {
        auto* mod = isSgModuleStatement(n);
    
        //retrieve the C file currently being created in translation.
        auto* c_file = getEnclosingFileNode(topScopeStack());
        
        (*module_map)[mod->get_name().getString()] = c_file;
    
        #if DEBUG
            cout << "XfVisitor::preOrderVisit::module_stmt (> "
                 << c_file->get_unparse_output_filename() << " defines module "
                 << mod->get_name().getString() << endl;
        #endif
    
        //DON'T SYNC, want to continue to the body.
        return;
    }
    
    if(isSgUseStatement(n)) {
        auto* use = isSgUseStatement(n);

        //retrieve the C file currently being created in translation.
        auto* c_gscope = getGlobalScope(topScopeStack());
        
        use_statements->push_back(pair<SgGlobal*,string>(c_gscope, use->get_name().getString()));

        #if DEBUG
            cout << "XfVisitor::preOrderVisit::use_stmt (> "
                 << getEnclosingFileNode(c_gscope)->get_unparse_output_filename()
                 << " uses module " << use->get_name().getString() << endl;
        #endif

        sync = n;
        return;
    }
           
    if(isSgProcedureHeaderStatement(n)) {
        xf_fn_decl(isSgProcedureHeaderStatement(n));
        sync = n;
        return;
    }
    
    if(isSgVariableDeclaration(n)) {
        xf_var_decl(isSgVariableDeclaration(n), arguments);
        sync = n;
        return;
    }
    
    if(isSgExprStatement(n)) {
        #if DEBUG
            cout << "XfVisitor::preOrderVisit::expr_stmt" << endl;
        #endif
        auto* expr = ftc::xf_expr(isSgExprStatement(n)->get_expression());
        xf_appendStatement(buildExprStatement(expr));
        sync = n;
        return;
    }
    
    if(isSgFortranDo(n)) {
        xf_fortran_do(isSgFortranDo(n));
        sync = n;
        return;
    }
    
    //ASSUMPTION: only functions have return statements; always in the format 'return' only.
    // --edit: Need to handle return statement in subprocedure :(
    if(isSgReturnStmt(n)) {
        #if DEBUG
            cout << "XfVisitor::preOrderVisit::return_stmt" << endl;
        #endif
        
        auto* fn_decl = getEnclosingFunctionDeclaration(n,true);
        assert(fn_decl!=NULL);
        assert(isSgProcedureHeaderStatement(fn_decl));
        
        auto* proc_h = isSgProcedureHeaderStatement(fn_decl);
        if(proc_h->isFunction())
            xf_appendStatement(buildReturnStmt(buildVarRefExp(SgName("__retv"))));
        else
            xf_appendStatement(buildReturnStmt(buildNullExpression()));
        
        sync = n;
        return;
    }
    
    if(isSgIfStmt(n)) {
        #if DEBUG
            cout << "XfVisitor::preOrderVisit::if_stmt" << endl;
        #endif
        auto* ifst = isSgIfStmt(n);
        
        assert(isSgExprStatement(ifst->get_conditional()));
        assert(isSgBasicBlock(ifst->get_true_body()));
        if(!isSgNullStatement(ifst->get_false_body()))
            assert(isSgBasicBlock(ifst->get_false_body()));
            
        auto* cond = isSgExprStatement(ifst->get_conditional())->get_expression();
        auto* if_cond = xf_expr(cond);
            
        auto* if_true = buildBasicBlock();
        
        SgStatement* if_false = NULL;
        if(isSgNullStatement(ifst->get_false_body()))
            if_false = buildNullStatement();
        else
            if_false = buildBasicBlock();
            
        auto* nif = buildIfStmt(
            if_cond,
            if_true,
            if_false
        );
        appendStatement(nif);
        
        xf_block(isSgBasicBlock(ifst->get_true_body()), if_true);
        if(!isSgNullStatement(ifst->get_false_body()))
            xf_block(isSgBasicBlock(ifst->get_false_body()), isSgBasicBlock(if_false));
                    
        sync = n;
        return;
    }
    
    if(isSgPrintStatement(n)) {
        auto* print = isSgPrintStatement(n);
        auto* lhs = print->get_format();
        assert(isSgAsteriskShapeExp(lhs));
        
        string format = "";
        
        vector<SgExpression*> ret_args;
        auto* stmts = print->get_io_stmt_list();
        auto exprs = stmts->get_expressions();
        
        for(auto i = exprs.begin(); i!=exprs.end(); i++) {
            auto* exp = *i;
            format.append(xf_format(exp));
            ret_args.push_back(xf_expr(exp));
        }
        format.append("\\n");
        
        ret_args.insert(ret_args.begin(),buildStringVal(format));
        
        auto* rprint = buildFunctionCallExp(
            buildFunctionRefExp("printf"),
            buildExprListExp(ret_args)
        );
        appendStatement(buildExprStatement(rprint));
        
        includes.insert(pair<string,bool>("stdio.h",true));
    
        sync = n;
        return;
    }
    
    if(isSgOpenStatement(n)) {
        auto* open = isSgOpenStatement(n);
        auto* file = open->get_file();
        auto* fileid = open->get_unit();
        
        includes.insert(pair<string,bool>("ftc_file_io.h",true));
        
        vector<SgExpression*> ret_args;
        ret_args.push_back(xf_expr(fileid));
        ret_args.push_back(xf_expr(file));
        
        auto* ropen = buildFunctionCallExp(
            buildFunctionRefExp("ftc__open_file"),
            buildExprListExp(ret_args)
        );
        appendStatement(buildExprStatement(ropen));
        
        sync = n;
        return;
    }
    
    if(isSgCloseStatement(n)) {
        auto* close = isSgCloseStatement(n);
        
        auto* fileid = close->get_unit();

        vector<SgExpression*> ret_args;
        ret_args.push_back(xf_expr(fileid));
        
        auto* rclose = buildFunctionCallExp(
            buildFunctionRefExp("ftc__close_file"),
            buildExprListExp(ret_args)
        );
        appendStatement(buildExprStatement(rclose));
        
        includes.insert(pair<string,bool>("ftc_file_io.h",true));
        
        sync = n;
        return;
    }
    
    if(isSgReadStatement(n)) {
        auto* read = isSgReadStatement(n);
        assert(isSgAsteriskShapeExp(read->get_format()));
        
        auto* fileid = read->get_unit();
        
        string format = "";
        
        vector<SgExpression*> ret_args;
        auto* stmts = read->get_io_stmt_list();
        auto exprs = stmts->get_expressions();
        
        for(auto i = exprs.begin(); i!=exprs.end(); i++) {
            auto* exp = *i;
            format.append(xf_format(exp));
            
            auto* nexp = xf_expr(exp);
            if(isSgPointerDerefExp(nexp))
                 nexp = isSgPointerDerefExp(nexp)->get_operand();
            else nexp = buildAddressOfOp(nexp);
                
            ret_args.push_back(nexp);
        }
        format.append("\\n");
        
        vector<SgExpression*> get_args;
        get_args.push_back(xf_expr(fileid));
        
        ret_args.insert(ret_args.begin(),buildStringVal(format));
        ret_args.insert(ret_args.begin(),
            buildFunctionCallExp(
                buildFunctionRefExp("ftc__get_file"),
                buildExprListExp(get_args)
            )
        );
        
        auto* rprint = buildFunctionCallExp(
            buildFunctionRefExp("fscanf"),
            buildExprListExp(ret_args)
        );
        appendStatement(buildExprStatement(rprint));
        
        includes.insert(pair<string,bool>("stdio.h",true));
        includes.insert(pair<string,bool>("ftc_file_io.h",true));
    
        sync = n;
        return;
    }
    
    cout << n->sage_class_name() << endl;
    throw (std::string)"XfVisitor::preOrderVisit non handled node type";
}
        
void ftc::XfVisitor::postOrderVisit(SgNode* n) {
    if(n==sync) sync = NULL;
}

///_________________________________________________________________________________________________
///

SgFile* ftc::xf_file(SgFile* file, SgProject** project, map<string,SgFile*>& module_map, vector<pair<SgGlobal*,string>>& use_statements) {
    assert(file!=NULL);
    assert(project!=NULL);
    
    ftc::module_map = &module_map;
    ftc::use_statements = &use_statements;
    
    if(file->get_outputLanguage()!=SgFile::e_Fortran_output_language) {
        throw (std::string)"Input file was not a Fortran file in tcf::xf_file :: SgFile* -> SgProject** -> SgFile*";
        return NULL;
    }
    
    //output file name: eg.f90 -> eg.c
    string fname = file->getFileName();
    fname.erase(fname.find_last_of('.'));
    fname.append(".c");
    
    #if DEBUG
        cout << "begin translation!" << endl;
    #endif
    
    //create new file, redirect for C unparsing
    string path = getFTCVar();
    path.append("dummy.f90");
    SgFile* outf = buildFile(path,fname,*project);
    outf->set_outputLanguage(SgFile::e_C_output_language);
    
    if(*project==NULL) *project = outf->get_project();
    
    //find SgGlobal for current and new file
    GFinder gfinder;
    
    SgGlobal* ngscope = gfinder.find(outf);
    SgGlobal* pgscope = gfinder.find(file);
    assert(ngscope!=NULL);
    assert(pgscope!=NULL);

    //push global scope for new file    
    xf_pushScopeStack(ngscope);
    
    //traverse AST of old file starting from global scope, and translate away!
    XfVisitor visitor;
    visitor.traverse(pgscope);
        
    xf_popScopeStack();
    
    //insert headers
    for(auto i = includes.begin(); i!=includes.end(); i++) {
        string incl = (*i).first;
        bool system = (*i).second;
        insertHeader(incl,  PreprocessingInfo::after, system, ngscope);
    }
    includes.clear();
    
    //always include math header
    insertHeader("math.h",  PreprocessingInfo::after, true, ngscope);    
    
    #if DEBUG
        cout << "translation complete!" << endl;
    #endif

    return outf;
}

///_________________________________________________________________________________________________
///

namespace ftc {
    class GenVisitor : public AstPrePostProcessing {
        //used when I want to skip the traversal of an entire sub-tree.
        //keep ignoring nodes until postOrderVisit is called with the sync node.
        SgNode* sync;
        
    public:
        GenVisitor() {
            sync = NULL;
        }

        void preOrderVisit(SgNode* n);
        void postOrderVisit(SgNode* n);
    };
}

void ftc::GenVisitor::preOrderVisit(SgNode* n) {
    if(sync!=NULL) return;
    
    //ignore list.
    if(isSgGlobal(n)) return;
    
    //want to skip any block scopes and only process global scope.
    if(isSgBasicBlock(n)) {
        sync = n;
        return;
    }
    
    if(isSgVariableDeclaration(n)) {
        #if DEBUG
            cout << "GenVisitor::preOrderVisit::var_decl" << endl;
        #endif
        
        auto* decl = isSgVariableDeclaration(n);
        auto vars = decl->get_variables();
        for(auto i = vars.begin(); i!=vars.end(); i++) {
            auto* init_name = *i;
            SgName var_name = init_name->get_name();
            #if DEBUG
                cout << ".. " << var_name.getString() << endl;
            #endif
            
            //onwards!
            SgType* var_type = init_name->get_type(); //use same type.
            auto* var_decl = buildVariableDeclaration(var_name,var_type);
            xf_appendStatement(var_decl);
            
            //make extern
           /* auto* out_name = var_decl->get_variables()[0];
            auto modifier = out_name->get_storageModifier();
            modifier.setExtern();
            
            modifier = var_decl->get_declarationModifier().get_storageModifier();
            modifier.setExtern();*/
            
            //neither of them work.. so let's hack it
            attachArbitraryText(var_decl, "extern",PreprocessingInfo::before);
        }
        
        sync = n;
        return;
    }
    
    if(isSgFunctionDeclaration(n)) {
        auto* decl = isSgFunctionDeclaration(n);
        SgName fn_name = decl->get_name();
        
        auto* fn_args = buildFunctionParameterList();
        auto args = decl->get_args();
        for(auto i = args.begin(); i!=args.end(); i++) {
            auto* init_name = *i;
            SgName arg_name = init_name->get_name();
            SgType* arg_type = init_name->get_type(); //reuse same type.

            xf_appendArg(fn_args, buildInitializedName(arg_name, arg_type));
        }
        
        SgType* fn_return = decl->get_type()->get_return_type(); //reuse same type.
        
        //buildNondefiningFunctionDeclaration returns a FORTRAN procedureHeaderStatement???
        //this is ANOTHER lovely hack to get this working.
        auto* fn_decl = buildDefiningFunctionDeclaration(fn_name, fn_return, fn_args);
        fn_decl->set_definition(NULL);
        appendStatement(fn_decl);
        attachArbitraryText(fn_decl,";",PreprocessingInfo::after);
        
        sync = n;
        return;
    }
}

void ftc::GenVisitor::postOrderVisit(SgNode* n) {
    if(n==sync) sync = NULL;
}

///_________________________________________________________________________________________________
///

SgFile* ftc::xf_gen_header(SgFile* file, SgProject* project) {
    assert(file!=NULL);
    assert(project!=NULL);
   
    if(file->get_outputLanguage()!=SgFile::e_C_output_language) {
        throw (std::string)"Input file was not a C file in txf::xf_gen_header";
        return NULL;
    }

    //output file name: eg.c -> eg.h
    string fname = file->get_unparse_output_filename();
    fname.erase(fname.find_last_of('.'));
    fname.append(".h");
    
    #if DEBUG
        cout << "begin generation!" << endl;
    #endif
    
    //create new file, redirect for C unparsing
    string path = getFTCVar();
    path.append("dummy.f90");
    SgFile* outf = buildFile(path,fname,project);
    outf->set_outputLanguage(SgFile::e_C_output_language);
    
    //find SgGlobal for current and new file
    GFinder gfinder;

    SgGlobal* ngscope = gfinder.find(outf);
    SgGlobal* pgscope = gfinder.find(file);
    assert(ngscope!=NULL);
    assert(pgscope!=NULL);

    //push global scope for new file    
    xf_pushScopeStack(ngscope);
    
    #if DEBUG
        cout << "#pragma once" << endl;
    #endif
    //buildPragmaDeclaration("once"); //doesn't work?
    attachArbitraryText(ngscope, "#pragma once");
    
    //traverse AST of C file starting from global scope, and generate away!
    GenVisitor visitor;
    visitor.traverse(pgscope);
        
    xf_popScopeStack();
    
    #if DEBUG
        cout << "generation complete!" << endl;
    #endif

    return outf;
}

