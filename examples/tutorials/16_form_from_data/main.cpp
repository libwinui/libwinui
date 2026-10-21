// SPDX-License-Identifier: MIT
// libwinui tutorial: a form built from data. A list of questions the user edits, a preview built
// from the same questions, and a table of answers whose columns follow them.
// --8<-- [start:includes]
#include <winui/winui.hpp>

#include <array>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <cstdint>
// --8<-- [end:includes]

namespace
{
// --8<-- [start:model]
enum class Kind
{
    Text,
    Number,
    Choice,
    YesNo,
};

constexpr std::array Kinds{Kind::Text, Kind::Number, Kind::Choice, Kind::YesNo};

const char* kind_name(Kind kind)
{
    switch (kind)
    {
        case Kind::Text:
            return "Text";
        case Kind::Number:
            return "Number";
        case Kind::Choice:
            return "Choice";
        case Kind::YesNo:
            return "Yes or no";
    }
    return "";
}

struct Choice
{
    explicit Choice(std::string t) : text{std::move(t)}
    {
    }
    ui::Property<std::string> text;
};

// One question: what the editor changes, and the answer the preview collects. Both sides bind to
// these Properties, so neither copies anything to the other.
struct Question
{
    Question(std::uint64_t question_id, Kind question_kind, std::string question) :
        id(question_id),
        kind{question_kind},
        label{std::move(question)}
    {
    }

    std::uint64_t                id;
    ui::Property<Kind>           kind;
    ui::Property<std::string>    label;
    ui::Property<bool>           required{false};
    ui::ObservableVector<Choice> choices;
    ui::Property<std::uint64_t>  after{0}; // shown once this question is answered; 0 for always

    // The answer, one Property per kind of input, and what the form makes of it.
    ui::Property<std::string>                text;
    ui::Property<std::optional<double>>      number;
    ui::Property<std::optional<std::size_t>> picked;
    ui::Property<std::optional<bool>>        yes{false};
    ui::Property<bool>                       shown{true};
    ui::Property<std::string>                error;

    std::vector<ui::Subscription> watches;
};
// --8<-- [end:model]

// --8<-- [start:form]
// A column of the answers table: which question, and its label when the columns were made.
struct Column
{
    std::uint64_t id = 0;
    std::string   label;
    friend bool   operator==(const Column&, const Column&) = default;
};

struct Response
{
    std::map<std::uint64_t, std::string> answers;
    friend bool                          operator==(const Response&, const Response&) = default;
};

struct Form
{
    ui::ObservableVector<Question>             questions;
    ui::Property<std::optional<std::uint64_t>> selected;
    // What a "show after" picker chooses from: 0 for "always", then every question, by id.
    ui::Property<std::vector<std::uint64_t>> after_ids{std::vector<std::uint64_t>{0}};
    ui::Property<std::vector<std::string>>   after_labels{std::vector<std::string>{"Always"}};
    ui::Property<std::vector<Column>>        columns;
    ui::ObservableVector<Response>           responses;
    ui::Property<std::string>                status;
    std::uint64_t                            next_id = 1;
    ui::Subscription                         watch;

    Form()
    {
        watch = questions.observe([this](const ui::CollectionChange&) { refresh(); });
    }

    Question& add(Kind kind, std::string label)
    {
        Question& q = questions.emplace_back(next_id++, kind, std::move(label));
        // The lists that name the questions follow their labels and kinds; whether each question
        // shows follows every answer it may depend on.
        q.watches.push_back(ui::observe([this](const auto&...) { refresh(); }, q.label, q.kind));
        q.watches.push_back(
            ui::observe([this](const auto&...) { update_shown(); }, q.after, q.text, q.number, q.picked, q.yes));
        refresh();
        return q;
    }

    void remove(std::uint64_t id)
    {
        if (selected.get() == id)
        {
            selected = std::nullopt;
        }
        questions.erase_if([id](const Question& q) { return q.id == id; });
    }

    Question* find(std::uint64_t id)
    {
        for (Question& q : questions)
        {
            if (q.id == id)
            {
                return &q;
            }
        }
        return nullptr;
    }

    static std::string answer(const Question& q)
    {
        switch (q.kind.get())
        {
            case Kind::Text:
                return q.text.get();
            case Kind::Number:
                return q.number.get() ? std::format("{}", *q.number.get()) : std::string{};
            case Kind::Choice:
            {
                const auto i = q.picked.get();
                return i && *i < q.choices.size() ? q.choices[*i].text.get() : std::string{};
            }
            case Kind::YesNo:
                return q.yes.get().value_or(false) ? "Yes" : "";
        }
        return {};
    }

    void refresh()
    {
        std::vector<std::uint64_t> ids{0};
        std::vector<std::string>   labels{"Always"};
        std::vector<Column>        cols;
        for (const Question& q : questions)
        {
            ids.push_back(q.id);
            labels.push_back("After: " + q.label.get());
            cols.push_back({.id = q.id, .label = q.label.get()});
        }
        after_ids    = std::move(ids);
        after_labels = std::move(labels);
        columns      = std::move(cols);
        update_shown();
    }

    // A question shows when it follows nothing, or when the one it follows shows and is answered.
    // It can only follow a question above it, so one pass from the top settles them all.
    void update_shown()
    {
        std::map<std::uint64_t, bool> answered;
        for (Question& q : questions)
        {
            const auto target = answered.find(q.after.get());
            q.shown           = q.after.get() == 0 || (target != answered.end() && target->second);
            answered[q.id]    = q.shown.get() && !answer(q).empty();
        }
    }

    void send()
    {
        Response response;
        bool     complete = true;
        for (Question& q : questions)
        {
            const std::string a = answer(q);
            q.error  = q.shown.get() && q.required.get() && a.empty() ? "This question needs an answer." : "";
            complete = complete && q.error.get().empty();
            if (q.shown.get())
            {
                response.answers[q.id] = a;
            }
        }
        if (!complete)
        {
            status = "Some answers are missing.";
            return;
        }
        responses.push_back(std::move(response));
        for (Question& q : questions)
        {
            q.text   = "";
            q.number = std::nullopt;
            q.picked = std::nullopt;
            q.yes    = false;
        }
        status = std::format("Sent. {} so far.", responses.size());
    }
};
// --8<-- [end:form]

// --8<-- [start:editor]
ui::Spec choice_row(Question& q, Choice& choice)
{
    return ui::Grid{
        .columns        = {ui::GridLength::fraction(), ui::GridLength::auto_()},
        .column_spacing = 4,
        .content =
            {
                ui::Cell{.column  = 0,
                         .content = ui::TextBox{.text = ui::bind(choice.text), .accessibility = {.name = "Choice"}}},
                ui::Cell{.column  = 1,
                         .content = ui::Button{.text     = "Remove",
                                               .on_click = [&q, &choice] { (void)q.choices.remove(choice); }}},
            },
    };
}

ui::Spec choices_editor(Question& q)
{
    ui::Spec rows = ui::ForEach<Choice>{
        .items  = q.choices,
        .layout = ui::StackLayout{.spacing = 4},
        .item   = [&q](Choice& choice) -> ui::Spec { return choice_row(q, choice); },
    };
    return ui::VerticalStack{
        .spacing = 4,
        .visible = ui::bind(q.kind, [](Kind k) { return k == Kind::Choice; }),
        .content =
            {
                ui::Text{.text = "Choices"},
                std::move(rows),
                ui::Button{.text = "Add a choice", .on_click = [&q] { q.choices.emplace_back("New choice"); }},
            },
    };
}

ui::Spec question_editor(Form& form, Question& q)
{
    std::vector<std::string> kinds;
    for (const Kind k : Kinds)
    {
        kinds.emplace_back(kind_name(k));
    }
    return ui::VerticalStack{
        .spacing = 12,
        .content =
            {
                // A Kind shown as an index: to() turns the model's value into the field's, from()
                // turns what the user picks back.
                ui::ComboBox{.header         = "Type",
                             .items          = std::move(kinds),
                             .selected_index = ui::bind(
                                 q.kind,
                                 [](Kind k) { return std::optional<std::size_t>{static_cast<std::size_t>(k)}; },
                                 [](const std::optional<std::size_t>& i) { return i ? Kinds[*i] : Kind::Text; })},
                ui::TextBox{.header = "Question", .text = ui::bind(q.label)},
                ui::CheckBox{.text = "Needs an answer", .checked = ui::bind(q.required)},
                choices_editor(q),
                // The question's id, picked from the ids as they are now: reordering the questions
                // moves the selection with them.
                ui::ComboBox{.header         = "Show",
                             .items          = ui::bind(form.after_labels),
                             .selected_index = ui::bind_choice(q.after, form.after_ids)},
                ui::Button{.text = "Delete the question", .on_click = [&form, id = q.id] { form.remove(id); }},
            },
    };
}

// Rebuilt for each selected question. It builds from the id it's given, not from form.selected or
// anything else that may be halfway through changing: a Match runs inside the write that changed
// its value.
ui::Spec editor(Form& form)
{
    return ui::Match<std::optional<std::uint64_t>>{
        .value   = ui::bind(form.selected),
        .content = [&form](const std::optional<std::uint64_t>& id) -> ui::Spec
        {
            Question* q = id ? form.find(*id) : nullptr;
            if (q == nullptr)
            {
                return ui::Text{.text = "Pick a question to change it."};
            }
            return question_editor(form, *q);
        },
    };
}
// --8<-- [end:editor]

ui::Spec outline(Form& form)
{
    ui::SpecList add;
    for (const Kind k : Kinds)
    {
        add.push_back(ui::Button{.text     = std::format("Add {}", kind_name(k)),
                                 .on_click = [&form, k] { form.selected = form.add(k, "New question").id; }});
    }
    ui::Spec list = ui::ListView<Question>{
        .items             = form.questions,
        .can_reorder_items = true,
        .accessibility     = {.name = "Questions"},
        .on_selection_changed =
            [&form](ui::SelectedItem<Question> index)
        {
            form.selected = index && *index < form.questions.size() ? std::optional{form.questions[*index].id} : std::nullopt;
        },
        .item = [](Question& q) -> ui::Spec { return ui::Text{.text = ui::bind(q.label)}; },
    };
    return ui::Grid{
        .rows = {ui::GridLength::auto_(), ui::GridLength::fraction()},
        .content =
            {
                ui::Cell{.row = 0, .content = ui::WrapPanel{.item_spacing = 4, .line_spacing = 4, .content = std::move(add)}},
                ui::Cell{.row = 1, .content = std::move(list)},
            },
    };
}

// --8<-- [start:preview]
// The question names the input: it's the input's header, which screen readers read as its name.
ui::Binding<std::string> header(Question& q)
{
    return ui::bind([](const std::string& label, bool required) { return required ? label + " *" : label; },
                    q.label,
                    q.required);
}

ui::Spec input(Question& q, Kind kind)
{
    switch (kind)
    {
        case Kind::Text:
            return ui::TextBox{.header = header(q), .text = ui::bind(q.text)};
        case Kind::Number:
            return ui::NumberBox{.header = header(q), .value = ui::bind(q.number)};
        case Kind::Choice:
            // The choices' texts, one Property in each item, as the list the drop-down shows.
            return ui::ComboBox{.header         = header(q),
                                .items          = ui::bind_each(q.choices, &Choice::text),
                                .selected_index = ui::bind(q.picked)};
        case Kind::YesNo:
            return ui::CheckBox{.text = header(q), .checked = ui::bind(q.yes)};
    }
    return {};
}

ui::Spec preview_question(Question& q)
{
    // A new type builds a new input; the answer lives in the question, so nothing is lost.
    return ui::Match<Kind>{
        .value   = ui::bind(q.kind),
        .visible = ui::bind(q.shown),
        .content = [&q](const Kind& kind) -> ui::Spec
        {
            return ui::VerticalStack{
                .spacing = 4,
                .content =
                    {
                        input(q, kind),
                        ui::Text{.text       = ui::bind(q.error),
                                 .foreground = ui::theme_colors::Critical,
                                 .visible    = ui::bind(q.error, [](const std::string& e) { return !e.empty(); })},
                    },
            };
        },
    };
}

ui::Spec preview(Form& form)
{
    ui::Spec questions = ui::ForEach<Question>{
        .items  = form.questions,
        .layout = ui::StackLayout{.spacing = 16},
        .item   = [](Question& q) -> ui::Spec { return preview_question(q); },
    };
    ui::Spec send = ui::HorizontalStack{
        .spacing = 12,
        .content =
            {
                ui::Button{.text = "Send", .variant = ui::ButtonVariant::Accent, .on_click = [&form] { form.send(); }},
                ui::Text{.text = ui::bind(form.status), .vertical_alignment = ui::Alignment::Center},
            },
    };
    return ui::Scroll{
        .content = ui::VerticalStack{.spacing = 20, .padding = 4, .content = {std::move(questions), std::move(send)}}};
}
// --8<-- [end:preview]

// --8<-- [start:answers]
ui::Spec answers_table(Form& form, const std::vector<Column>& columns)
{
    std::vector<ui::TableColumn<Response>> table_columns;
    for (const Column& c : columns)
    {
        table_columns.push_back(
            {.header = c.label,
             .width  = ui::AutoSize,
             .text   = [id = c.id](const Response& r)
             {
                 const auto a = r.answers.find(id);
                 return a == r.answers.end() ? std::string{} : a->second;
             }});
    }
    return ui::TableView<Response>{
        .items         = form.responses,
        .columns       = std::move(table_columns),
        .empty_content = ui::Text{.text = "Answers you send show here."},
    };
}

// A table's columns are fixed when it's built, so the table is rebuilt when they change. The Match
// keys on the columns themselves: an edit that leaves them as they were rebuilds nothing.
ui::Spec answers(Form& form)
{
    return ui::Match<std::vector<Column>>{
        .value   = ui::bind(form.columns),
        .content = [&form](const std::vector<Column>& columns) -> ui::Spec { return answers_table(form, columns); },
    };
}
// --8<-- [end:answers]

void sample(Form& form)
{
    form.add(Kind::Text, "Your name").required = true;
    Question& talk                             = form.add(Kind::YesNo, "I'd like to give a talk");
    Question& topic                            = form.add(Kind::Text, "What about?");
    topic.after                                = talk.id;
    Question& length                           = form.add(Kind::Choice, "How long?");
    length.choices.emplace_back("5 minutes");
    length.choices.emplace_back("15 minutes");
    length.after = talk.id;
    form.add(Kind::Number, "Guests you're bringing");
}
} // namespace

// --8<-- [start:main]
int main()
{
    Form form;
    sample(form);

    ui::Window window{{.title = "A form built from data", .size = {1200, 780}}};
    ui::Spec   panes = ui::Grid{
        .columns        = {ui::GridLength::dip(260), ui::GridLength::dip(300), ui::GridLength::fraction()},
        .column_spacing = 24,
        .content =
            {
                ui::Cell{.column = 0, .content = outline(form)},
                ui::Cell{.column = 1, .content = ui::Scroll{.content = editor(form)}},
                ui::Cell{.column = 2, .content = preview(form)},
            },
    };
    window.set_content(ui::Grid{
        .rows        = {ui::GridLength::fraction(), ui::GridLength::dip(200)},
        .row_spacing = 16,
        .padding     = 16,
        .content =
            {
                ui::Cell{.row = 0, .content = std::move(panes)},
                ui::Cell{.row = 1, .content = answers(form)},
            },
    });
    return ui::run(window);
}
// --8<-- [end:main]
