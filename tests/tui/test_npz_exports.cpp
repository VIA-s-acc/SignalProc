#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "io/npy.hpp"
#include "support/paths.hpp"
#include "tui/shell/plot_export.hpp"
#include "tui/ui_fixture.hpp"

TEST_CASE("a chart exported to npz keeps every series", "[tui][export]") {
    auto ui = signals::test::make_ui(150, 44);
    const auto path = signals::test::temp_path("chart.npz");
    std::filesystem::remove(path);
    std::string notice;
    ui->ask_text = [&](std::string, std::string, std::function<void(std::string)> done) { done(path.string()); };
    ui->notice = [&](std::string t) { notice = std::move(t); };
    signals::tui::ChartExport chart;
    chart.id = "energy";
    chart.title = "Energy";
    chart.chart.series.push_back({"a", "#000000", {0, 1, 2}, {3, 4, 5}});
    chart.chart.series.push_back({"b", "#000000", {0, 1}, {7, 8}});
    signals::tui::export_chart(*ui, chart);
    const auto read = signals::io::read_npz(path);
    REQUIRE(read);
    CHECK(std::get<std::vector<double>>(signals::io::np_find(*read, "x0")->data) == std::vector<double>{0, 1, 2});
    CHECK(std::get<std::vector<double>>(signals::io::np_find(*read, "y1")->data) == std::vector<double>{7, 8});
    CHECK(std::get<std::vector<std::string>>(signals::io::np_find(*read, "names")->data) == std::vector<std::string>{"a", "b"});
    CHECK(notice.find("chart.npz") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(signals::test::temp_path("chart.csv")));
}

TEST_CASE("new npz prompts fit an 80-column palette", "[tui][export]") {
    for (const std::string lang : {"ru", "en"}) {
        std::ifstream in{std::filesystem::path{SIGNALS_SOURCE_DIR} / "resources" / "i18n" / (lang + ".json")};
        const auto d = nlohmann::json::parse(in);
        for (const std::string key : {"tui.pipeline.probe.save_prompt", "tui.decomposition.npz_prompt", "tui.filters.npz_prompt", "tui.export.prompt",
                                      "tui.signal.rate_prompt", "tui.cmd.open_prompt"}) {
            INFO(lang << " " << key);
            const auto text = d.at(key).get<std::string>();
            std::size_t columns = 0;
            for (const unsigned char c : text) columns += (c & 0xC0) != 0x80;
            CHECK(columns <= 70);
        }
    }
}
