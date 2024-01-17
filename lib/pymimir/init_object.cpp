
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"


using namespace py::literals;

void init_object(py::module& m)
{
    py::class_<mimir::formalism::ObjectImpl, mimir::formalism::Object> object(m, "Object");

    object.def(py::init(&mimir::formalism::create_object), "id"_a, "name"_a, "type"_a, "Creates a new object with the given id, name and type.");
    object.def_readonly("id", &mimir::formalism::ObjectImpl::id, "Gets the identifier of the object.");
    object.def_readonly("name", &mimir::formalism::ObjectImpl::name, "Gets the name of the object.");
    object.def_readonly("type", &mimir::formalism::ObjectImpl::type, "Gets the type of the object.");
    object.def("is_variable", &mimir::formalism::ObjectImpl::is_free_variable, "Returns whether the term is a variable.");
    object.def("is_constant", &mimir::formalism::ObjectImpl::is_constant, "Returns whether the term is a constant.");
    object.def("__repr__", [](const mimir::formalism::ObjectImpl& object) { return "<Object '" + object.name + "'>"; });
}