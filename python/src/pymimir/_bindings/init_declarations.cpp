#include "init_declarations.hpp"

ClassMap& TypeRegister::map()
{
    static ClassMap type_map;
    return type_map;
}
