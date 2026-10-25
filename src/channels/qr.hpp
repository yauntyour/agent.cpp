#pragma once
#include "channels/common.hpp"
#include "channels/qrcodegen.hpp"

    namespace bot
    {
    inline std::string qr_svg(const std::string &text)
    {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        int size = qr.getSize();
        int border = 4;
        int total = size + 2 * border;
        std::ostringstream oss;
        oss << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << total << " " << total
            << "\" shape-rendering=\"crispEdges\">";
        oss << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>";
        oss << "<path d=\"";
        bool first = true;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                if (qr.getModule(x, y))
                {
                    if (!first)
                        oss << " ";
                    oss << "M" << (x + border) << "," << (y + border) << "h1v1h-1z";
                    first = false;
                }
        oss << "\" fill=\"black\"/>";
        oss << "</svg>";
        return oss.str();
    }
} // namespace bot
