#pragma once

enum class HttpError {
    None = 0,
    BadRequest,            // 400
    UriTooLong,            // 414
    HeaderTooLarge,        // 431
    PayloadTooLarge,       // 413
    NotImplemented         // 501
};