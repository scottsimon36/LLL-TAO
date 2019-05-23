Finance API
-----------------------------------

The Finance API provides methods for sending and receiving NXS coins (tokens) between users / accounts, creating accounts, and managing staking. 


## `Named Shortcuts`

For each API method we support an alternative endpoint that includes the account name or register address at the end of the the URL.  This shortcut removes the need to include the account name or address as an additional parameter.  

For example `finance/create/account/savings` is a shortcut to `finance/create/account?name=savings`.

Similarly `finance/get/account/5efc8a9437d93e894defd50f8ba73a0b2b5529cc593d5e4a7ea413022a9c35e9` is a shortcut to `finance/get/account?address=5efc8a9437d93e894defd50f8ba73a0b2b5529cc593d5e4a7ea413022a9c35e9`  

The logic for resolving the shortcut to either a name or address is that if the data is 64 characters of hexadecimal then it will be assumed to be a register address.  Otherwise it will be considered a name.  


## `Methods`

The following methods are currently supported by this API

[`create/account`](#createaccount)   
[`debit/account`](#debitaccount)   
[`credit/account`](#creditaccount)   
[`get/account`](#getaccount)   
[`list/accounts`](#listaccounts)   

-----------------------------------
***

# `create/account`

Create a new account for receiving NXS. The API supports an alternative endpoint that can include the account name in the URL.  For example `/finance/create/account/savings` will resolve to `finance/create/account?name=savings`.   


### Endpoint:

`/finance/create/account`


### Parameters:

`pin` : The PIN for this signature chain.

`session` : For multi-user API mode, (configured with multiuser=1) the session is required to identify which session (sig-chain) the account should be created with. For single-user API mode the session should not be supplied.

`name` : A name to identify the account. A hash of the name will determine the register address. If no name is provided, a random 256-bit register hash will be generated.
  


### Example:

This example creates an account called "main".  

```
{
    "pin": "1234",
    "session": "16953804956473956578",
    "name": "main"
}
```

### Return value JSON object:
```    
{
    "txid": "f9dcd28bce2563ab288fab76cf3ee5149ea938c735894ce4833b55e474e08e8a519e8005e09e2fc19623577a8839a280ca72b6430ee0bdf13b3d9f785bc7397d",
    "address": "f2f71204bda6e6512d21c48eb4ca98db37a25c4d152afb284f3fc36760c0c717"
}
```   

### Return values:

`txid` : The ID (hash) of the transaction that includes the account creation.

`address` : The register address for this  account. The address (or name that hashes to this address) is needed when creating crediting or debiting the account.    


***

# `debit/account`

Deduct an amount of NXS from an account and send it to another account. The API supports an alternative endpoint that can include the account name in the URL.  For example `/finance/debit/account/savings` will resolve to `finance/debit/account?name=savings`.   


### Endpoint:

`/finance/debit/account`


### Parameters:

`pin` : The PIN for this signature chain.

`session` : For multi-user API mode, (configured with multiuser=1) the session is required to identify which session (sig-chain) the account owner. For single-user API mode the session should not be supplied.

`name` : The name identifying the account to debit. This is optional if the address is provided.  The name should be in namespace:name format

`namespace` : The namespace that the the account was create in. This is optional if the address is provided or the name is provided in the namespace:name format.

`address` : The register address of the account to debit. This is optional if the name is provided.

`amount` : The amount of NXS to debit.

`name_to` : The name identifying the account to send to. This is optional if the address is provided.  The name should be in namespace:name format

`address_to` : The register address of the account to send to. This is optional if the name is provided.

### Example:

The following example shows the json payload for a debit of 10.5 NXS from an account called "main" (that itself was created by the sig chain logged into session "16953804956473956578") being sent to an account identified by the name "bob:savings".  In the name_to field, "bob" refers to the username of the account holder and "savings" is their NXS account.  If Bob is concerned about privacy he could have provided the register address of his token account instead

```
{
    "pin": "1234",
    "session": "16953804956473956578",
    "name": "main",
    "amount": 10.5,
    "name_to": "bob:savings"
}
```

### Return value JSON object:
```    
{
    "txid": "f9dcd28bce2563ab288fab76cf3ee5149ea938c735894ce4833b55e474e08e8a519e8005e09e2fc19623577a8839a280ca72b6430ee0bdf13b3d9f785bc7397d"
}
```   

### Return values:

`txid` : The ID (hash) of the transaction that includes the debit.

****

# `credit/account`

Increment an amount received from another NXS account to an account owned by your signature chain. The API supports an alternative endpoint that can include the account name to credit in the URL.  For example `/finance/credit/account/savings` will resolve to `finance/credit/account?name=savings`.   


### Endpoint:

`/finance/credit/account`


### Parameters:

`pin` : The PIN for this signature chain.

`session` : For multi-user API mode, (configured with multiuser=1) the session is required to identify which session (sig-chain) owns the account. For single-user API mode the session should not be supplied.

`name` : The name identifying the account to credit. This is optional if the address is provided.  The name should be in namespace:name format

`namespace` : The namespace that the the account was create in. This is optional if the address is provided or the name is provided in the namespace:name format.

`address` : The register address of the account to credit. This is optional if the name is provided.

`amount` : The amount of NXS to credit.

`txid` : The transaction ID (hash) of the corresponding debit transaction for which you are creating this credit for.

### Example:

The following example shows the json payload for a credit of 10 NXS to an account called "savings" (that itself was created by the sig chain logged into session "16953804956473956578") identified by the name "bob:savings".  In the name field, "bob" refers to the username of the account holder and "savings" is their NXS account.  

```
{
    "pin": "1234",
    "session": "16953804956473956578",
    "name": "bob:savings",
    "amount": 10,
    "txid": "f9dcd28bce2563ab288fab76cf3ee5149ea938c735894ce4833b55e474e08e8a519e8005e09e2fc19623577a8839a280ca72b6430ee0bdf13b3d9f785bc7397d"
}
```

### Return value JSON object:
```    
{
    "txid": "318b86d2c208618aaa13946a3b75f14472ebc0cce9e659f2830b17e854984b55606738f689d886800f21ffee68a3e5fd5a29818e88f8c5b13b9f8ae67739903d"
}
```   

### Return values:

`txid` : The ID (hash) of the transaction that includes the credit.

****



# `get/account`

Retrieves information about a NXS account . The API supports an alternative endpoint that can include the account name in the URL.  For example `/finance/get/account/savings` will resolve to `finance/get/account?name=savings`. 

Additionally the API supports passing a field name in the URL after the account name, which will populate the `fieldname` parameter in the request and filter the response on that field.  For example `/finance/get/account/savings/balance` will resolve to `finance/get/account?name=savings&fieldname=balance`


### Endpoint:

`/finance/get/account`


### Parameters:

`session` : For multi-user API mode, (configured with multiuser=1) the session is required to identify which session (sig-chain) owns the account. For single-user API mode the session should not be supplied.

`name` : The name identifying the NXS account to retrieve. This is optional if the address is provided.  The name should be in namespace:name format.  If session is provided that the namespace is not required as it will be deduced from the signature chain of the session

`namespace` : The namespace that the the account was create in. This is optional if the address is provided, if the name is provided in the namespace:name format, or of the session is provided

`address` : The register address of the NXS account to retrieve. This is optional if the name is provided.

`fieldname`: This optional field can be used to filter the response to return only a single field from the account.

### Return value JSON object:
```    
{
    "address": "5efc8a9437d93e894defd50f8ba73a0b2b5529cc593d5e4a7ea413022a9c35e9",
    "identifier": "NXS",
    "balance": 990
}
```   

### Return values:

`address` : The register address of the account.  The address is a hash based on the owners username and account name.

`identifier` : For NXS accounts the identifier is always "NXS".    

`balance` : The current balance of this account.  


***

# `list/accounts`

This will list off all of the NXS accounts belonging to the currently logged in signature chain. 


### Endpoint:

`/finance/list/accounts`


### Parameters:

`session` : For multi-user API mode, (configured with multiuser=1) the session is required to identify which session (sig-chain) owns the account. For single-user API mode the session should not be supplied.


### Return value JSON object:
```
[
    {
        "address": "f9ee3e63c845794d577d54c5087f0ff2f2053195bf007907e8134977bb1f1e24",
        "identifier": "NXS",
        "balance": 5000
    },
    {
        "address": "a74774dc075e59d03639b56da8c29736fe248888a43c6103060409dd11273417",
        "identifier": "NXS",
        "balance": 60
    }

]
```
### Return values:

`address` : The register address for the account

`identifer` : This is always `NXS`

`balance` : The current balance of the token account.

****