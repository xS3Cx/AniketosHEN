#pragma once

/* What Home's rows of tiles hold. AppBrowseItemAccessor.BuildWhereClause is
 * hooked for the Games and Media rows and the lists of the custom tabs we
 * add: this side knows which titles the user put in those tabs or in Games
 * and Media, which titles were moved out of the stock rows, and which apps
 * are hidden. The system's own app database is only read. */
namespace HomeRows {
void Install();
/* True when the database query hook is in place. */
bool Ready();
}
