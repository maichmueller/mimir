#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "trampolines.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace mimir;

decltype(auto) tuple_fwd(auto&&... args) { return std::forward_as_tuple(FWD(args)...); }

void init_pymimir(py::module& m)
{
    // order is important either for dependencies in return types of bound functions or for stubgen!
    auto enums = std::tuple(  //
        py::enum_<loki::RequirementEnum>(m, "RequirementEnum"),
        py::enum_<loki::AssignOperatorEnum>(m, "AssignOperatorEnum"),
        py::enum_<loki::BinaryOperatorEnum>(m, "BinaryOperatorEnum"),
        py::enum_<loki::MultiOperatorEnum>(m, "MultiOperatorEnum"),
        py::enum_<loki::OptimizationMetricEnum>(m, "OptimizationMetricEnum"),
        py::enum_<SearchNodeStatus>(m, "SearchNodeStatus"),
        py::enum_<SearchStatus>(m, "SearchStatus"),
        py::enum_<ObjectGraphPruningStrategyEnum>(m, "ObjectGraphPruningStrategyEnum")  //
    );

    auto [cls_requirementsimpl,  //
          cls_objectimpl,
          cls_variableimpl,
          cls_termobjectimpl,
          cls_termvariant,
          cls_staticpredicateimpl,
          cls_fluentpredicateimpl,
          cls_derivedpredicateimpl,
          cls_staticatomimpl,
          cls_fluentatomimpl,
          cls_derivedatomimpl,
          cls_pddl_factories,
          cls_staticgroundatomimpl,
          cls_fluentgroundatomimpl,
          cls_derivedgroundatomimpl,
          cls_staticgroundliteralimpl,
          cls_fluentgroundliteralimpl,
          cls_derivedgroundliteralimpl,
          cls_staticliteralimpl,
          cls_fluentliteralimpl,
          cls_derivedliteralimpl,
          cls_axiomimpl,
          cls_numericfluentimpl,
          cls_effectsimpleimpl,
          cls_effectcompleximpl,
          cls_stripsactioneffect,
          cls_simplefluenteffect,
          cls_flatbitset,
          cls_functionskeletonimpl,
          cls_functionimpl,
          cls_groundfunctionimpl,
          cls_functionexpressionnumberimpl,
          cls_functionexpressionbinaryoperatorimpl,
          cls_functionexpressionmultioperatorimpl,
          cls_functionexpressionminusimpl,
          cls_functionexpressionfunctionimpl,
          cls_functionexpressionvariant,
          cls_groundfunctionexpressionvariant,
          cls_groundfunctionexpressionnumberimpl,
          cls_groundfunctionexpressionbinaryoperatorimpl,
          cls_groundfunctionexpressionmultioperatorimpl,
          cls_groundfunctionexpressionminusimpl,
          cls_groundfunctionexpressionfunctionimpl,
          cls_optimizationmetricimpl,
          cls_actionimpl,
          cls_groundactionimpl,
          cls_domainimpl,
          cls_problemimpl,
          cls_pddlparser,
          cls_stateimpl,
          cls_staterepository,
          cls_stripsactionprecondition,
          cls_conditionaleffect,
          cls_liftedconjunctiongrounder,
          cls_iapplicableactiongenerator,
          cls_iliftedapplicableactiongeneratoreventhandler,
          cls_defaultliftedapplicableactiongeneratoreventhandler,
          cls_debugliftedapplicableactiongeneratoreventhandler,
          cls_liftedapplicableactiongenerator,
          cls_igroundedapplicableactiongeneratoreventhandler,
          cls_defaultgroundedapplicableactiongeneratoreventhandler,
          cls_debuggroundedapplicableactiongeneratoreventhandler,
          cls_groundedapplicableactiongenerator,
          cls_iheuristic,
          cls_blindheuristic,
          cls_ialgorithm,
          cls_astaralgorithmstatistics,
          cls_iastaralgorithmeventhandler,
          cls_defaultastaralgorithmeventhandler,
          cls_debugastaralgorithmeventhandler,
          cls_astaralgorithmeventhandlerbase,
          cls_astaralgorithm,
          cls_brfsalgorithmstatistics,
          cls_ibrfsalgorithmeventhandler,
          cls_defaultbrfsalgorithmeventhandler,
          cls_debugbrfsalgorithmeventhandler,
          cls_brfsalgorithm,
          cls_tupleindexmapper,
          cls_iwalgorithmstatistics,
          cls_iiwalgorithmeventhandler,
          cls_defaultiwalgorithmeventhandler,
          cls_iwalgorithm,
          cls_siwalgorithmstatistics,
          cls_isiwalgorithmeventhandler,
          cls_defaultsiwalgorithmeventhandler,
          cls_siwalgorithm,
          cls_statevertex,
          cls_groundactionedge,
          cls_groundactionsedge,
          cls_statespaceoptions,
          cls_statespacesoptions,
          cls_statespace,
          cls_faithfulabstractionoptions,
          cls_faithfulabstractionsoptions,
          cls_faithfulabstractstatevertex,
          cls_faithfulabstraction,
          cls_globalfaithfulabstractstate,
          cls_globalfaithfulabstraction,
          cls_abstraction,
          cls_emptyvertex,
          cls_emptyedge,
          cls_staticdigraph,
          cls_nautycertificate,
          cls_nautydensegraph,
          cls_nautysparsegraph,
          cls_colorfunction,
          cls_problemcolorfunction,
          cls_coloredvertex,
          cls_staticvertexcoloreddigraph,
          cls_tuplegraphvertex,
          cls_tuplegraph,
          cls_tuplegraphfactory,
          cls_objectgraphpruningstrategy,
          cls_certificatecolorrefinement,
          cls_certificate2fwl,
          cls_certificate3fwl,
          cls_certificate4fwl,
          cls_isomorphismtypecompressionfunction
          //
    ] = std::tuple(                                       //
        py::class_<RequirementsImpl>(m, "Requirements"),  //
        py::class_<ObjectImpl>(m, "Object"),
        py::class_<VariableImpl>(m, "Variable"),
        py::class_<TermObjectImpl>(m, "TermObject"),
        py::class_<pymimir::TermVariant>(m, "Term"),
        py::class_<PredicateImpl<Static>>(m, "StaticPredicate"),
        py::class_<PredicateImpl<Fluent>>(m, "FluentPredicate"),
        py::class_<PredicateImpl<Derived>>(m, "DerivedPredicate"),
        py::class_<AtomImpl<Static>>(m, "StaticAtom"),
        py::class_<AtomImpl<Fluent>>(m, "FluentAtom"),
        py::class_<AtomImpl<Derived>>(m, "DerivedAtom"),
        py::class_<PDDLFactories, std::shared_ptr<PDDLFactories>>(m, "PDDLFactories"),
        py::class_<GroundAtomImpl<Static>>(m, "StaticGroundAtom"),
        py::class_<GroundAtomImpl<Fluent>>(m, "FluentGroundAtom"),
        py::class_<GroundAtomImpl<Derived>>(m, "DerivedGroundAtom"),
        py::class_<GroundLiteralImpl<Static>>(m, "StaticGroundLiteral"),
        py::class_<GroundLiteralImpl<Fluent>>(m, "FluentGroundLiteral"),
        py::class_<GroundLiteralImpl<Derived>>(m, "DerivedGroundLiteral"),
        py::class_<LiteralImpl<Static>>(m, "StaticLiteral"),
        py::class_<LiteralImpl<Fluent>>(m, "FluentLiteral"),
        py::class_<LiteralImpl<Derived>>(m, "DerivedLiteral"),
        py::class_<AxiomImpl>(m, "Axiom"),
        py::class_<NumericFluentImpl>(m, "NumericFluent"),
        py::class_<EffectSimpleImpl>(m, "EffectSimple"),
        py::class_<EffectComplexImpl>(m, "EffectComplex"),
        py::class_<StripsActionEffect>(m, "StripsActionEffect"),
        py::class_<SimpleFluentEffect>(m, "SimpleFluentEffect"),
        py::class_<FlatBitset>(m, "FlatBitset"),
        py::class_<FunctionSkeletonImpl>(m, "FunctionSkeleton"),
        py::class_<FunctionImpl>(m, "Function"),
        py::class_<GroundFunctionImpl>(m, "GroundFunction"),
        py::class_<FunctionExpressionNumberImpl>(m, "FunctionExpressionNumber"),
        py::class_<FunctionExpressionBinaryOperatorImpl>(m, "FunctionExpressionBinaryOperator"),
        py::class_<FunctionExpressionMultiOperatorImpl>(m, "FunctionExpressionMultiOperator"),
        py::class_<FunctionExpressionMinusImpl>(m, "FunctionExpressionMinus"),
        py::class_<FunctionExpressionFunctionImpl>(m, "FunctionExpressionFunction"),
        py::class_<pymimir::FunctionExpressionVariant>(m, "FunctionExpression"),
        py::class_<pymimir::GroundFunctionExpressionVariant>(m, "GroundFunctionExpression"),
        py::class_<GroundFunctionExpressionNumberImpl>(m, "GroundFunctionExpressionNumber"),
        py::class_<GroundFunctionExpressionBinaryOperatorImpl>(m, "GroundFunctionExpressionBinaryOperator"),
        py::class_<GroundFunctionExpressionMultiOperatorImpl>(m, "GroundFunctionExpressionMultiOperator"),
        py::class_<GroundFunctionExpressionMinusImpl>(m, "GroundFunctionExpressionMinus"),
        py::class_<GroundFunctionExpressionFunctionImpl>(m, "GroundFunctionExpressionFunction"),
        py::class_<OptimizationMetricImpl>(m, "OptimizationMetric"),
        py::class_<ActionImpl>(m, "Action"),
        py::class_<GroundActionImpl>(m, "GroundAction"),
        py::class_<DomainImpl>(m, "Domain"),
        py::class_<ProblemImpl>(m, "Problem"),
        py::class_<PDDLParser>(m, "PDDLParser"),
        py::class_<StateImpl>(m, "State"),
        py::class_<StateRepository, std::shared_ptr<StateRepository>>(m, "StateRepository"),
        py::class_<StripsActionPrecondition>(m, "StripsActionPrecondition"),
        py::class_<ConditionalEffect>(m, "ConditionalEffect"),
        py::class_<LiftedConjunctionGrounder, std::shared_ptr<LiftedConjunctionGrounder>>(m, "LiftedConjunctionGrounder"),
        py::class_<IApplicableActionGenerator, std::shared_ptr<IApplicableActionGenerator>>(m, "IApplicableActionGenerator"),
        py::class_<ILiftedApplicableActionGeneratorEventHandler, std::shared_ptr<ILiftedApplicableActionGeneratorEventHandler>>(
            m,
            "ILiftedApplicableActionGeneratorEventHandler"),
        py::class_<DefaultLiftedApplicableActionGeneratorEventHandler,
                   ILiftedApplicableActionGeneratorEventHandler,
                   std::shared_ptr<DefaultLiftedApplicableActionGeneratorEventHandler>>(m, "DefaultLiftedApplicableActionGeneratorEventHandler"),
        py::class_<DebugLiftedApplicableActionGeneratorEventHandler,
                   ILiftedApplicableActionGeneratorEventHandler,
                   std::shared_ptr<DebugLiftedApplicableActionGeneratorEventHandler>>(m, "DebugLiftedApplicableActionGeneratorEventHandler"),
        py::class_<LiftedApplicableActionGenerator, IApplicableActionGenerator, std::shared_ptr<LiftedApplicableActionGenerator>>(
            m,
            "LiftedApplicableActionGenerator"),
        py::class_<IGroundedApplicableActionGeneratorEventHandler, std::shared_ptr<IGroundedApplicableActionGeneratorEventHandler>>(
            m,
            "IGroundedApplicableActionGeneratorEventHandler"),
        py::class_<DefaultGroundedApplicableActionGeneratorEventHandler,
                   IGroundedApplicableActionGeneratorEventHandler,
                   std::shared_ptr<DefaultGroundedApplicableActionGeneratorEventHandler>>(m, "DefaultGroundedApplicableActionGeneratorEventHandler"),
        py::class_<DebugGroundedApplicableActionGeneratorEventHandler,
                   IGroundedApplicableActionGeneratorEventHandler,
                   std::shared_ptr<DebugGroundedApplicableActionGeneratorEventHandler>>(m, "DebugGroundedApplicableActionGeneratorEventHandler"),
        py::class_<GroundedApplicableActionGenerator, IApplicableActionGenerator, std::shared_ptr<GroundedApplicableActionGenerator>>(
            m,
            "GroundedApplicableActionGenerator"),
        py::class_<IHeuristic, IPyHeuristic, std::shared_ptr<IHeuristic>>(m, "IHeuristic").def(py::init<>()),
        py::class_<BlindHeuristic, IHeuristic, std::shared_ptr<BlindHeuristic>>(m, "BlindHeuristic").def(py::init<>()),
        py::class_<IAlgorithm, std::shared_ptr<IAlgorithm>>(m, "IAlgorithm"),
        py::class_<AStarAlgorithmStatistics>(m, "AStarAlgorithmStatistics"),
        py::class_<IAStarAlgorithmEventHandler, std::shared_ptr<IAStarAlgorithmEventHandler>>(m, "IAStarAlgorithmEventHandler"),
        py::class_<DefaultAStarAlgorithmEventHandler, IAStarAlgorithmEventHandler, std::shared_ptr<DefaultAStarAlgorithmEventHandler>>(
            m,
            "DefaultAStarAlgorithmEventHandler"),
        py::class_<DebugAStarAlgorithmEventHandler, IAStarAlgorithmEventHandler, std::shared_ptr<DebugAStarAlgorithmEventHandler>>(
            m,
            "DebugAStarAlgorithmEventHandler"),
        py::class_<DynamicAStarAlgorithmEventHandlerBase,
                   IAStarAlgorithmEventHandler,
                   IPyDynamicAStarAlgorithmEventHandlerBase,
                   std::shared_ptr<DynamicAStarAlgorithmEventHandlerBase>>(m, "AStarAlgorithmEventHandlerBase"),
        py::class_<AStarAlgorithm, IAlgorithm, std::shared_ptr<AStarAlgorithm>>(m, "AStarAlgorithm"),
        py::class_<BrFSAlgorithmStatistics>(m, "BrFSAlgorithmStatistics"),
        py::class_<IBrFSAlgorithmEventHandler, std::shared_ptr<IBrFSAlgorithmEventHandler>>(m, "IBrFSAlgorithmEventHandler"),
        py::class_<DefaultBrFSAlgorithmEventHandler, IBrFSAlgorithmEventHandler, std::shared_ptr<DefaultBrFSAlgorithmEventHandler>>(
            m,
            "DefaultBrFSAlgorithmEventHandler"),
        py::class_<DebugBrFSAlgorithmEventHandler, IBrFSAlgorithmEventHandler, std::shared_ptr<DebugBrFSAlgorithmEventHandler>>(
            m,
            "DebugBrFSAlgorithmEventHandler"),
        py::class_<BrFSAlgorithm, IAlgorithm, std::shared_ptr<BrFSAlgorithm>>(m, "BrFSAlgorithm"),
        py::class_<TupleIndexMapper, std::shared_ptr<TupleIndexMapper>>(m, "TupleIndexMapper"),
        py::class_<IWAlgorithmStatistics>(m, "IWAlgorithmStatistics"),
        py::class_<IIWAlgorithmEventHandler, std::shared_ptr<IIWAlgorithmEventHandler>>(m, "IIWAlgorithmEventHandler"),
        py::class_<DefaultIWAlgorithmEventHandler, IIWAlgorithmEventHandler, std::shared_ptr<DefaultIWAlgorithmEventHandler>>(m,
                                                                                                                              "DefaultIWAlgorithmEventHandler"),
        py::class_<IWAlgorithm, IAlgorithm, std::shared_ptr<IWAlgorithm>>(m, "IWAlgorithm"),
        py::class_<SIWAlgorithmStatistics>(m, "SIWAlgorithmStatistics"),
        py::class_<ISIWAlgorithmEventHandler, std::shared_ptr<ISIWAlgorithmEventHandler>>(m, "ISIWAlgorithmEventHandler"),
        py::class_<DefaultSIWAlgorithmEventHandler, ISIWAlgorithmEventHandler, std::shared_ptr<DefaultSIWAlgorithmEventHandler>>(
            m,
            "DefaultSIWAlgorithmEventHandler"),
        py::class_<SIWAlgorithm, IAlgorithm, std::shared_ptr<SIWAlgorithm>>(m, "SIWAlgorithm"),
        py::class_<StateVertex>(m, "StateVertex"),
        py::class_<GroundActionEdge>(m, "GroundActionEdge"),
        py::class_<GroundActionsEdge>(m, "GroundActionsEdge"),
        py::class_<StateSpaceOptions>(m, "StateSpaceOptions"),
        py::class_<StateSpacesOptions>(m, "StateSpacesOptions"),
        py::class_<StateSpace, std::shared_ptr<StateSpace>>(m, "StateSpace"),
        py::class_<FaithfulAbstractionOptions>(m, "FaithfulAbstractionOptions"),
        py::class_<FaithfulAbstractionsOptions>(m, "FaithfulAbstractionsOptions"),
        py::class_<FaithfulAbstractStateVertex>(m, "FaithfulAbstractStateVertex"),
        py::class_<FaithfulAbstraction, std::shared_ptr<FaithfulAbstraction>>(m, "FaithfulAbstraction"),
        py::class_<GlobalFaithfulAbstractState>(m, "GlobalFaithfulAbstractState"),
        py::class_<GlobalFaithfulAbstraction, std::shared_ptr<GlobalFaithfulAbstraction>>(m, "GlobalFaithfulAbstraction"),
        py::class_<Abstraction, std::shared_ptr<Abstraction>>(m, "Abstraction"),
        py::class_<EmptyVertex>(m, "EmptyVertex"),
        py::class_<EmptyEdge>(m, "EmptyEdge"),
        py::class_<StaticDigraph>(m, "StaticDigraph"),
        py::class_<nauty_wrapper::Certificate, std::shared_ptr<nauty_wrapper::Certificate>>(m, "NautyCertificate"),
        py::class_<nauty_wrapper::DenseGraph>(m, "NautyDenseGraph"),
        py::class_<nauty_wrapper::SparseGraph>(m, "NautySparseGraph"),
        py::class_<ColorFunction>(m, "ColorFunction"),
        py::class_<ProblemColorFunction, ColorFunction>(m, "ProblemColorFunction"),
        py::class_<ColoredVertex>(m, "ColoredVertex"),
        py::class_<StaticVertexColoredDigraph>(m, "StaticVertexColoredDigraph"),
        py::class_<TupleGraphVertex>(m, "TupleGraphVertex"),
        py::class_<TupleGraph>(m, "TupleGraph"),
        py::class_<TupleGraphFactory>(m, "TupleGraphFactory"),
        py::class_<ObjectGraphPruningStrategy>(m, "ObjectGraphPruningStrategy"),
        py::class_<color_refinement::Certificate>(m, "CertificateColorRefinement"),
        py::class_<kfwl::Certificate<2>>(m, "Certificate2FWL"),
        py::class_<kfwl::Certificate<3>>(m, "Certificate3FWL"),
        py::class_<kfwl::Certificate<4>>(m, "Certificate4FWL"),
        py::class_<kfwl::IsomorphismTypeCompressionFunction>(m, "IsomorphismTypeCompressionFunction")
        //
    );

    std::apply(init_enums(m, enums));
    init_requirements(m);
    init_object(m);
    init_variable(m);
    init_termobject(m);
    init_termvariable(m);
    init_termvariant(m);
    init_predicates(m);
    init_atoms(m);
    init_pddl_factories(m);
    init_ground_atoms(m);
    init_literals(m);
    init_axiom(m);
    init_numeric_fluent(m);
    init_effects(m);
    init_flatbitset(m);
    init_function(m);
    init_function_expression(m);
    init_ground_function_expression(m);
    init_optimization_metric(m);
    init_actions(m);

    init_domain(m);
    init_problem(m);

    py::class_<PDDLParser>(m, "PDDLParser")  //
        .def(py::init<std::string, std::string>(), py::arg("domain_path"), py::arg("problem_path"))
        .def("get_domain", &PDDLParser::get_domain, py::return_value_policy::reference_internal)
        .def("get_problem", &PDDLParser::get_problem, py::return_value_policy::reference_internal)
        .def("get_pddl_factories", &PDDLParser::get_pddl_factories);

    init_state(m);
    init_strips_action_precondition(m);
    init_conditional_effect(m);
    init_aag(m);
    init_heuristics(m);
    init_algorithms(m);
    init_state_space(m);
    init_abstraction(m);
    init_static_digraph(m);
    init_nauty_wrappers(m);
    init_static_vertexcolored_graph(m);
    init_tuple_graph(m);

    // ObjectGraph
    m.def("create_object_graph",
          &create_object_graph,
          py::arg("color_function"),
          py::arg("pddl_factories"),
          py::arg("problem"),
          py::arg("state"),
          py::arg("state_index") = 0,
          py::arg("mark_true_goal_literals") = false,
          py::arg("pruning_strategy") = ObjectGraphPruningStrategy(),
          "Creates an object graph based on the provided parameters");
    // Color Refinement
    py::class_<color_refinement::Certificate>(m, "CertificateColorRefinement")
        .def("__eq__", [](const color_refinement::Certificate& lhs, const color_refinement::Certificate& rhs) { return lhs == rhs; })
        .def("__hash__", [](const color_refinement::Certificate& self) { return std::hash<color_refinement::Certificate>()(self); })
        .def("__str__",
             [](const color_refinement::Certificate& self)
             {
                 auto os = std::stringstream();
                 os << self;
                 return os.str();
             })
        // Returning canonical compression functions does not work due to unhashable type list.
        //.def("get_canonical_configuration_compression_function", &color_refinement::Certificate::get_canonical_compression_function)
        .def("get_canonical_coloring", &color_refinement::Certificate::get_canonical_coloring);

    m.def("compute_certificate_color_refinement",
          &color_refinement::compute_certificate<StaticVertexColoredDigraph>,
          py::arg("graph"),
          "Creates color refinement certificate");

    // K-FWL
    auto bind_kfwl_certificate = [&]<size_t K>(const std::string& class_name, std::integral_constant<size_t, K>)
    {
        using CertificateK = kfwl::Certificate<K>;

        py::class_<CertificateK>(m, class_name.c_str())
            .def("__eq__", [](const CertificateK& lhs, const CertificateK& rhs) { return lhs == rhs; })
            .def("__hash__", [](const CertificateK& self) { return std::hash<CertificateK>()(self); })
            .def("__str__",
                 [](const CertificateK& self)
                 {
                     auto os = std::stringstream();
                     os << self;
                     return os.str();
                 })
            // Returning canonical compression functions does not work due to unhashable type list.
            //.def("get_canonical_isomorphic_type_compression_function", &CertificateK::get_canonical_isomorphic_type_compression_function)
            //.def("get_canonical_configuration_compression_function", &CertificateK::get_canonical_configuration_compression_function)
            .def("get_canonical_coloring", &CertificateK::get_canonical_coloring);
    };
    bind_kfwl_certificate("Certificate2FWL", std::integral_constant<size_t, 2> {});
    bind_kfwl_certificate("Certificate3FWL", std::integral_constant<size_t, 3> {});
    bind_kfwl_certificate("Certificate4FWL", std::integral_constant<size_t, 4> {});

    py::class_<kfwl::IsomorphismTypeCompressionFunction>(m, "IsomorphismTypeCompressionFunction")  //
        .def(py::init<>());

    auto bind_compute_kfwl_certificate = [&]<size_t K>(const std::string& function_name, std::integral_constant<size_t, K>)
    {
        m.def(
            function_name.c_str(),
            [](const StaticVertexColoredDigraph& graph, kfwl::IsomorphismTypeCompressionFunction& iso_type_function)
            { return kfwl::compute_certificate<K>(graph, iso_type_function); },
            py::arg("static_vertex_colored_digraph"),
            py::arg("isomorphism_type_compression_function"));
    };
    bind_compute_kfwl_certificate("compute_certificate_2fwl", std::integral_constant<size_t, 2> {});
    bind_compute_kfwl_certificate("compute_certificate_3fwl", std::integral_constant<size_t, 3> {});
    bind_compute_kfwl_certificate("compute_certificate_4fwl", std::integral_constant<size_t, 4> {});
}