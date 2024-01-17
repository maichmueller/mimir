
#include "init.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "mimir/search/openlists/open_list_base.hpp"
#include "mimir/search/openlists/priority_queue_open_list.hpp"

void init_open_list(py::module& m)
{
    py::class_<mimir::planners::OpenListBase<int32_t>, mimir::planners::OpenList> open_list(m, "OpenList");

    py::class_<mimir::planners::PriorityQueueOpenList<int32_t>, std::shared_ptr<mimir::planners::PriorityQueueOpenList<int32_t>>> priority_queue_open_list(m, "PriorityQueueOpenList", open_list);
    priority_queue_open_list.def(py::init(&mimir::planners::create_priority_queue_open_list), "Creates a priority queue open list object.");

}