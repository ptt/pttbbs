# Account Registration

## Configuring

### Enabled methods

The file `etc/reg.methods` stores enabled account verification methods.
The file can be changed without a recompliation.
If the file does not exist, no verification methods will be usable.

In the file, text after '`#`', spaces and tabs are considered comments.
Hence the method names must start at the beginning of each line.
The available methods are:

- `manual`
- `email`
- `sms`

See `sample/etc/reg.methods` for an example.

### `manual` method

FIXME: Fill me in.

### `email` method

FIXME: Fill me in.

### `sms` method

- Set up the user agreement: `etc/reg.sms.notes`

In order to use the `sms` method, it is required to put an user agreement at
`etc/reg.sms.notes`. Without this file, the user will see an error.

- Set up the compile time defines in `pttbbs.conf`

Set the following settings:

```
#define USE_SMS_VERIFICATION
#define SMS_PHONE_NUMBER_LEN (10)
#define SMS_INSERT_SECRET ""
#define SMS_INSERT_SERVER_ADDR "127.0.0.1:80"
#define SMS_INSERT_HOST SMS_INSERT_SERVER_ADDR
#define SMS_INSERT_URI "/sms/insert"
#define SMS_URL "https://www.ptt.cc/sms"
```

`SMS_URL` is optional, and others are required.
Please see the next bullet for details of the setting values.

- Implement your SMS backend

The current code is hard-coded to accept mobile phone numbers in Taiwan.
`mbbsd` will accept phone numbers in the format of `0912345678`,
and internally converts it to the E.164 format; for example, the former number
will be converted to `886912345678`. If you wish to accept numbers from other
countries you will need to modify the `ValidatePhone` function in
`mbbsd/register_sms.cc`.

Once the user inputs a valid number, it will be checked against `verifydb`.
If the number is not currently used, it will send a HTTP POST request to the
following URL configured.

```
http://{SMS_INSERT_SERVER_ADDR}{SMS_INSERT_URI}
```

You may set a different `Host` header using `SMS_INSERT_HOST`.

**Request:**
The request body will be a url-encoded form content
(`application/x-www-form-urlencoded`) containing the following fields.

- `secret`: the `SMS_INSERT_SECRET`
- `phone`: the phone number in E.164 format
- `ip`: the IP address of the user (e.g. `127.0.0.1`)
- `code`: the OTP code

**Response:**
The response will be considered successful if the response code is `200` and
contains a session ID header named `X-Aotp-Session-Id`.
This session ID will be logged to `log/register_sms.log` for debugging
purposes.

If your system ...

- ... directly sends the code to user's phone, then `SMS_URL` shall not be set
and the user will be asked to input the code directly.
- ... requires further user interactions to release the code, then configure
`SMS_URL` for `mbbsd` to show and it will wait for code input from user.

The following URL will be display to the user if `SMS_URL` is set.

```
{SMS_URL}?id={SESSION_ID}
```

where the `SESSION_ID` is the value of the mentioned HTTP response header.
