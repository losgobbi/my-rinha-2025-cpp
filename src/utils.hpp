#pragma once

void json_dump(const std::string &name, const Json::Value &jsonBody) {
#ifdef JSON_DUMP
    Json::StyledWriter styledWriter;
    std::cout << name << " | received JSON:\n" << styledWriter.write(jsonBody);
#endif
}

int64_t parseIsoMs(const std::string& s) {
    std::chrono::sys_time<std::chrono::milliseconds> tp{};
    std::istringstream in(s);
    in >> std::chrono::parse("%FT%T", tp);
    return tp.time_since_epoch().count();
}
