/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef MIMIR_SEARCH_SATISFICING_BINDING_GENERATORS_EVENT_HANDLERS_DEFAULT_HPP_
#define MIMIR_SEARCH_SATISFICING_BINDING_GENERATORS_EVENT_HANDLERS_DEFAULT_HPP_

#include "mimir/search/satisficing_binding_generators/event_handlers/interface.hpp"

namespace mimir::search::satisficing_binding_generator
{
class DefaultEventHandlerImpl : public EventHandlerBase<DefaultEventHandlerImpl>
{
private:
    /* Implement EventHandlerBase interface */
    friend class EventHandlerBase<DefaultEventHandlerImpl>;

    void on_valid_base_binding_impl(const formalism::ObjectList& binding) const;
    void on_valid_derived_binding_impl(const formalism::ObjectList& binding) const;

    void on_invalid_base_binding_impl(const formalism::ObjectList& binding) const;
    void on_invalid_derived_binding_impl(const formalism::ObjectList& binding) const;

    void on_end_search_impl() const;

    void on_finish_search_layer_impl() const;

public:
    explicit DefaultEventHandlerImpl(bool quiet = true);

    static DefaultEventHandler create(bool quiet = true);
};

class NullEventHandlerImpl final : public IEventHandler
{
private:
    Statistics m_statistics;

public:
    void on_valid_base_binding(const formalism::ObjectList& binding) override {}
    void on_valid_derived_binding(const formalism::ObjectList& binding) override {}

    void on_invalid_base_binding(const formalism::ObjectList& binding) override {}
    void on_invalid_derived_binding(const formalism::ObjectList& binding) override {}

    void on_end_search() override {}

    void on_finish_search_layer() override {}

    const Statistics& get_statistics() const override { return m_statistics; }

    static EventHandler create() { return std::make_shared<NullEventHandlerImpl>(); }
};
}

#endif
