/**
*/

#include "BRHederaWallet.h"

#include <stdlib.h>
#include <assert.h>
#include "support/BRArray.h"
#include "BRHederaAddress.h"
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>

struct BRHederaWalletRecord {
    BRHederaAccount account;
    BRHederaUnitTinyBar balance;
    BRHederaFeeBasis feeBasis;
    BRArrayOf(BRHederaAddress)  nodes;

    BRArrayOf(BRHederaTransaction) transactions;

    pthread_mutex_t lock;
};

extern BRHederaWallet
hederaWalletCreate (BRHederaAccount account)
{
    assert(account);
    BRHederaWallet wallet = calloc(1, sizeof(struct BRHederaWalletRecord));
    wallet->account = account;
    // TODO - do we just hard code Hedera nodes here - we probably can for now
    // but perhaps in the future there will be additional shards/realms
    /*
     {"0.0.3":"104.196.1.78:50211", "0.0.4": "35.245.250.134:50211",
     "0.0.5":"34.68.209.35:50211", "0.0.6": "34.82.173.33:50211",
     "0.0.7":"35.200.105.230:50211", "0.0.8": "35.203.87.206:50211",
     "0.0.9":"35.189.221.159:50211", "0.0.10": "35.234.104.86:50211",
     "0.0.11":"34.90.238.202:50211", "0.0.12": "35.228.11.53:50211",
     "0.0.13":"35.234.132.107:50211", "0.0.14": "34.94.67.202:50211",
     "0.0.15":"35.236.2.27:50211"}
     */
    array_new(wallet->nodes, HEDERA_NODE_COUNT);
    // int64_t hedera_node_start = HEDERA_NODE_START;
    for (int i = HEDERA_NODE_START; i < (HEDERA_NODE_COUNT + HEDERA_NODE_START); i++) {
        array_add(wallet->nodes, hederaAddressCreate(0, 0, i));
    }

    array_new(wallet->transactions, 0);

    // Add a default fee basis
    wallet->feeBasis.costFactor = 1;
    wallet->feeBasis.pricePerCostFactor = 500000;

    return wallet;
}

extern void
hederaWalletFree (BRHederaWallet wallet)
{
    assert(wallet);
    for (int i = 0; i < array_count(wallet->nodes); i++) {
        hederaAddressFree(wallet->nodes[i]);
    }
    array_free(wallet->nodes);
    free(wallet);
}

extern BRHederaAddress
hederaWalletGetSourceAddress (BRHederaWallet wallet)
{
    assert(wallet);
    assert(wallet->account);
    // In the Hedera system there is only a single address per account
    // No need to clone the address here since the account code will do that
    return hederaAccountGetPrimaryAddress(wallet->account);
}

extern BRHederaAddress
hederaWalletGetTargetAddress (BRHederaWallet wallet)
{
    assert(wallet);
    assert(wallet->account);
    // In the Hedera system there is only a single address per account
    // No need to clone the address here since the account code will do that
    return hederaAccountGetPrimaryAddress(wallet->account);
}

extern void
hederaWalletSetBalance (BRHederaWallet wallet, BRHederaUnitTinyBar balance)
{
    assert(wallet);
    wallet->balance = balance;
}

extern BRHederaUnitTinyBar
hederaWalletGetBalance (BRHederaWallet wallet)
{
    assert(wallet);
    return (wallet->balance);
}

extern BRHederaAddress
hederaWalletGetNodeAddress(BRHederaWallet wallet)
{
    static unsigned index = 0;
    assert(wallet);
    if (index > HEDERA_NODE_COUNT - 1) {
        index = 0;
    }
    BRHederaAddress node = wallet->nodes[index++];
    return hederaAddressClone(node);
}

extern void
hederaWalletSetDefaultFeeBasis (BRHederaWallet wallet, BRHederaFeeBasis feeBasis)
{
    assert(wallet);
    wallet->feeBasis = feeBasis;
}

extern BRHederaFeeBasis
hederaWalletGetDefaultFeeBasis (BRHederaWallet wallet)
{
    assert(wallet);
    return wallet->feeBasis;
}

static bool
walletHasTransfer (BRHederaWallet wallet, BRHederaTransaction transaction) {
    bool r = false;
    for (size_t index = 0; index < array_count(wallet->transactions) && false == r; index++) {
        r = hederaTransactionEqual (transaction, wallet->transactions[index]);
    }
    return r;
}

extern int hederaWalletHasTransfer (BRHederaWallet wallet, BRHederaTransaction transfer) {
    pthread_mutex_lock (&wallet->lock);
    int result = walletHasTransfer (wallet, transfer);
    pthread_mutex_unlock (&wallet->lock);
    return result;
}

extern void hederaWalletAddTransfer(BRHederaWallet wallet, BRHederaTransaction transaction)
{
    assert(wallet);
    assert(transaction);
    pthread_mutex_lock (&wallet->lock);
    if (!walletHasTransfer(wallet, transaction)) {
        transaction = hederaTransactionClone(transaction);
        array_add(wallet->transactions, transaction);

        // Update the balance
        BRHederaUnitTinyBar amount = (hederaTransactionHasError(transaction)
                                      ? 0
                                      : hederaTransactionGetAmount(transaction));
        BRHederaUnitTinyBar fee    = hederaTransactionGetFee(transaction);

        BRHederaAddress accountAddress = hederaAccountGetAddress(wallet->account);
        BRHederaAddress source = hederaTransactionGetSource(transaction);
        BRHederaAddress target = hederaTransactionGetTarget(transaction);

        int isSource = hederaAccountHasAddress (wallet->account, source);
        int isTarget = hederaAccountHasAddress (wallet->account, target);

        if (isSource && isTarget)
            wallet->balance -= fee;
        else if (isSource)
            wallet->balance -= (amount + fee);
        else if (isTarget)
            wallet->balance += amount;
        else {
            // something is seriously wrong
        }
        // Cleanpu
        hederaAddressFree (source);
        hederaAddressFree (target);
        hederaAddressFree (accountAddress);
    }
    pthread_mutex_unlock (&wallet->lock);
}

extern void hederaWalletRemTransfer (BRHederaWallet wallet,
                                     OwnershipKept BRHederaTransaction transaction)
{
    assert(wallet);
    assert(transaction);
    pthread_mutex_lock (&wallet->lock);
    if (walletHasTransfer(wallet, transaction)) {
        for (size_t index = 0; index < array_count(wallet->transactions); index++)
            if (hederaTransactionEqual (transaction, wallet->transactions[index])) {
                hederaTransactionFree(wallet->transactions[index]);
                array_rm (wallet->transactions, index);
                break;
            }

        // Update the balance
        BRHederaUnitTinyBar amount = (hederaTransactionHasError(transaction)
                                      ? 0
                                      : hederaTransactionGetAmount(transaction));
        BRHederaUnitTinyBar fee    = hederaTransactionGetFee(transaction);

        BRHederaAddress accountAddress = hederaAccountGetAddress(wallet->account);
        BRHederaAddress source = hederaTransactionGetSource(transaction);
        BRHederaAddress target = hederaTransactionGetTarget(transaction);

        int isSource = hederaAccountHasAddress (wallet->account, source);
        int isTarget = hederaAccountHasAddress (wallet->account, target);

        if (isSource && isTarget)
            wallet->balance += fee;
        else if (isSource)
            wallet->balance += (amount + fee);
        else if (isTarget)
            wallet->balance -= amount;
        else {
            // something is seriously wrong
        }
        // Cleanup
        hederaAddressFree (source);
        hederaAddressFree (target);
        hederaAddressFree (accountAddress);
    }
    pthread_mutex_unlock (&wallet->lock);
}

extern int
hederaWalletHasAddress (BRHederaWallet wallet,
                        BRHederaAddress address)
{
    return hederaAccountHasAddress (wallet->account, address);
}
