/* Our rows of the Toolbox, inside toolbox_xml.cpp's anonymous namespace. */

void append_cheats_visual_options(ps5ui::Group& g) {
  g.label("id_cheats_visual_about",
          toolbox_i18n::tr("cheats.visual_menu.about"))
      .toggle("id_cheats_visual_menu",
              toolbox_i18n::tr("cheats.visual_menu.enabled"),
              toolbox_on("id_cheats_visual_menu"), std::nullopt,
              toolbox_i18n::tr("cheats.visual_menu.enabled.desc"))
      .list("id_cheats_visual_render",
            toolbox_i18n::tr("cheats.visual_menu.render"),
            [](ps5ui::ListBuilder& L) {
              L.item("id_cheats_visual_render_pui2",
                     toolbox_i18n::tr("cheats.visual_menu.render.pui2"), "0")
                  .item("id_cheats_visual_render_pui3",
                        toolbox_i18n::tr("cheats.visual_menu.render.pui3"),
                        "1");
            },
            toolbox_i18n::tr("cheats.visual_menu.render.sub"),
            toolbox_val("id_cheats_visual_render"));
}

/* One row per place: its mark, its name and the handle under it; pressing a
   row shows the address (onpress_aniketos.cpp). */
void append_find_me_rows(ps5ui::Group& g) {
  g.label("id_find_me_info", toolbox_i18n::tr("about.find_me.info"),
          ps5ui::Style::Center)
      .button("id_find_me_x", "X", "@Alex0_SH", std::nullopt, kIconSocialX)
      .button("id_find_me_youtube", "YouTube", "AlexZero", std::nullopt,
              kIconSocialYouTube)
      .button("id_find_me_discord", "Discord", "alex0sh", std::nullopt,
              kIconSocialDiscord)
      .button("id_find_me_telegram", "Telegram", "@missgoodpussy",
              std::nullopt, kIconSocialTelegram)
      .button("id_find_me_tiktok", "TikTok", "@mealexzero", std::nullopt,
              kIconSocialTikTok);
}
