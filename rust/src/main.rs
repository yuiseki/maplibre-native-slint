mod maplibre;

slint::include_modules!();

fn main() {
    let ui = MapWindow::new().unwrap();
    let map = maplibre::create_map(ui.get_map_size());

    maplibre::init(&ui, &map);

    ui.run().unwrap();
}
