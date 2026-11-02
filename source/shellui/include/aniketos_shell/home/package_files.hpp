#pragma once

#include <string>
#include <vector>

/* The places packages can be in (/data and the external drives that are
 * plugged in), the .pkg files of each, and the system's own "install this
 * package file" call, the one its Package Installer screen makes:
 * sceAppInstUtilInstallByPackage, found by name. The system does the rest,
 * and shows the progress in Downloads. */
namespace PackageFiles {

/* ShellUI has its own view of the file system: /data is /user/data there and
 * the USB drives are /usb0 to /usb7, so a place has the folder as this process
 * reaches it and the name it is known by everywhere else, which is what is
 * shown. */
struct Place {
  std::string folder; /* as ShellUI sees it */
  std::string shown;
  int depth; /* how many folders down packages are looked for */
  bool drive;
  std::vector<int> packages; /* their numbers in Listing::packages */
};

struct Listing {
  std::vector<Place> places;
  std::vector<std::string> packages;
};

Listing Find();
/* The file's path inside its place: "pkg/Name.pkg", "Name.pkg". */
std::string Label(const std::string &place, const std::string &path);
/* The system's result, 0 when it took the package; -1 when the call cannot be
 * reached. */
int Install(const std::string &path);

}
