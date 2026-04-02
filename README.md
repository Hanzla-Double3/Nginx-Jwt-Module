# NGINX JWT Authentication Module

A dynamic NGINX module written in C that verifies JWT tokens, reads the secret from a `.env` file, and automatically adds JWT claims as HTTP headers.

## Features

- JWT token verification using HS256 (HMAC SHA-256)
- Reads JWT secret from `.env` file
- Automatically extracts claims and adds them as HTTP headers
- Supports multiple claim types (string, integer, boolean, arrays, objects)
- Bearer token authentication
- Dynamic module - no need to recompile NGINX

## Prerequisites

The module requires the following dependencies:

### Arch Linux
```bash
sudo pacman -S --needed base-devel pcre zlib openssl libjwt jansson wget nginx
```

### Ubuntu/Debian
```bash
sudo apt-get install -y build-essential libpcre3-dev zlib1g-dev libssl-dev libjwt-dev libjansson-dev wget nginx
```

### Fedora/RHEL
```bash
sudo dnf install -y gcc make pcre-devel zlib-devel openssl-devel libjwt-devel jansson-devel wget nginx
```

## Installation

### 1. Install Dependencies

```bash
# For Arch Linux
make deps-arch

# For Ubuntu/Debian
make deps

# For Fedora/RHEL
make deps-fedora
```

### 2. Build the Module

```bash
make build
```

This will:
- Download NGINX source code (matching your installed version if possible)
- Configure NGINX with the JWT module
- Build the dynamic module

### 3. Install the Module

```bash
sudo make install NGINX_MODULES_PATH=/usr/lib/nginx/modules
```

Adjust `NGINX_MODULES_PATH` to match your system's NGINX modules directory:
- Arch Linux: `/usr/lib/nginx/modules`
- Ubuntu/Debian: `/usr/share/nginx/modules`
- Fedora/RHEL: `/usr/lib64/nginx/modules`

## Configuration

### 1. Load the Module

Add to the top of your `nginx.conf` (before the `http` block):

```nginx
load_module modules/ngx_http_jwt_module.so;
```

### 2. Configure the .env File

Create a `.env` file in your NGINX directory (or specify a custom path):

```bash
JWT_SECRET=your-256-bit-secret-key
```

**Important:** Keep this secret secure and never commit it to version control!

### 3. Enable JWT Authentication

Add to your location block:

```nginx
http {
    server {
        listen 80;
        server_name example.com;

        location /api {
            jwt_auth on;
            jwt_env_path /path/to/.env;  # Optional, defaults to .env

            # Your backend
            proxy_pass http://localhost:3000;
        }

        location /public {
            # No JWT required here
            proxy_pass http://localhost:3000;
        }
    }
}
```

## Usage

### Making Requests

Send requests with a JWT token in the Authorization header:

```bash
curl -H "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..." \
     http://example.com/api/users
```

### JWT Claims as Headers

The module automatically extracts all JWT claims and adds them as headers with the prefix `X-JWT-Claim-`:

**Example JWT Payload:**
```json
{
  "sub": "1234567890",
  "name": "John Doe",
  "admin": true,
  "iat": 1516239022,
  "roles": ["admin", "user"]
}
```

**Resulting Headers:**
```
X-JWT-Claim-sub: 1234567890
X-JWT-Claim-name: John Doe
X-JWT-Claim-admin: true
X-JWT-Claim-iat: 1516239022
X-JWT-Claim-roles: ["admin","user"]
```

Your backend application can now access these headers to get user information without decoding the JWT again.

## Testing

### 1. Generate a Test JWT

You can use https://jwt.io or generate one programmatically:

```python
import jwt

payload = {
    'sub': '1234567890',
    'name': 'John Doe',
    'admin': True
}

secret = 'your-256-bit-secret-key'
token = jwt.encode(payload, secret, algorithm='HS256')
print(token)
```

### 2. Test with curl

```bash
# Valid token
curl -v -H "Authorization: Bearer YOUR_TOKEN_HERE" http://localhost/api

# Invalid token (should return 401)
curl -v -H "Authorization: Bearer invalid.token.here" http://localhost/api

# No token (should return 401)
curl -v http://localhost/api
```

### 3. Test Backend Header Reception

Create a simple test backend to verify headers:

```python
# test_server.py
from flask import Flask, request
app = Flask(__name__)

@app.route('/api/test')
def test():
    headers = dict(request.headers)
    jwt_claims = {k: v for k, v in headers.items() if k.startswith('X-Jwt-Claim-')}
    return {'received_claims': jwt_claims}

if __name__ == '__main__':
    app.run(port=3000)
```

## Configuration Directives

### `jwt_auth`

- **Syntax:** `jwt_auth on | off;`
- **Default:** `off`
- **Context:** `http`, `server`, `location`

Enables or disables JWT authentication for the location.

### `jwt_env_path`

- **Syntax:** `jwt_env_path path;`
- **Default:** `.env`
- **Context:** `http`, `server`, `location`

Specifies the path to the `.env` file containing the JWT secret.

## Security Considerations

1. **Secret Management:** Store your JWT secret securely. Never commit `.env` files to version control.
2. **HTTPS:** Always use HTTPS in production to prevent token interception.
3. **Token Expiration:** Include `exp` claim in your JWTs and validate it on the backend.
4. **Secret Rotation:** Implement a strategy for rotating JWT secrets.
5. **File Permissions:** Ensure `.env` file has restricted permissions:
   ```bash
   chmod 600 .env
   chown nginx:nginx .env
   ```

## Troubleshooting

### Module fails to load

Check NGINX error log:
```bash
sudo tail -f /var/log/nginx/error.log
```

Common issues:
- Missing dependencies: Install `libjwt` and `libjansson`
- Wrong module path: Verify `load_module` path matches installation location
- NGINX version mismatch: Ensure module was built with compatible NGINX version

### 401 Unauthorized errors

- Verify JWT secret matches between token generation and `.env` file
- Check token format: Must be `Bearer <token>`
- Ensure token is not expired
- Verify token signature algorithm is HS256

### Claims not appearing as headers

- Check NGINX error log for JWT parsing errors
- Verify backend is looking for headers with `X-JWT-Claim-` prefix
- Test with simple payload first before complex nested claims

## Development

### Project Structure

```
nginx-jwt/
├── ngx_http_jwt_module.c  # Main module source
├── config                  # NGINX module config
├── Makefile               # Build system
├── .env                   # JWT secret (DO NOT COMMIT)
└── README.md              # This file
```

### Rebuilding

```bash
make clean
make build
```

## References

- [NGINX Module Development](https://nginx.org/en/docs/dev/development_guide.html)
- [libjwt Documentation](https://github.com/benmcollins/libjwt)
- [JWT.io](https://jwt.io)
